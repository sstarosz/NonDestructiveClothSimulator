#include "clothcad/core/File.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "internal/DocumentAccess.h"
#include "internal/ExprNode.h"
#include "internal/FieldAccess.h"

namespace clothcad {

namespace {

using Kind = ExprNode::Kind;

// The operation types by the names they have in a file, in OpData's order.
// A name, once released, must keep meaning the same type.
constexpr std::array<std::string_view, std::variant_size_v<OpData>> typeNames = {
    "Rect", "Circle", "RingSector", "Instance", "Offcut", "Curve",
    "Bezier", "Line", "LineBetween", "Cut", "Mirror", "AdjustPoint", "AddPoint", "Scale",
};
// Units by their names in a file, in Unit's order; never renamed once released.
constexpr std::array<std::string_view, 4> unitNames = {"length", "angle", "fraction", "none"};
static_assert(static_cast<std::size_t>(Unit::None) == 3);

static_assert(std::is_same_v<std::variant_alternative_t<0, OpData>, Rect>);
static_assert(std::is_same_v<std::variant_alternative_t<4, OpData>, Offcut>);
static_assert(std::is_same_v<std::variant_alternative_t<9, OpData>, Cut>);
static_assert(std::is_same_v<std::variant_alternative_t<11, OpData>, AdjustPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<12, OpData>, AddPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<13, OpData>, Scale>);

template <std::size_t... I>
std::optional<OpData> emptyOperation(std::size_t index, std::index_sequence<I...>) {
    std::optional<OpData> made;
    ((index == I ? (made.emplace(std::in_place_index<I>), true) : false) || ...);
    return made;
}
std::optional<OpData> emptyOperation(std::string_view name) {
    for (std::size_t i = 0; i < typeNames.size(); ++i)
        if (typeNames[i] == name) return emptyOperation(i, std::make_index_sequence<std::variant_size_v<OpData>>{});
    return std::nullopt;
}

// ---- Writing ----

std::string number(double value) {
    char text[40];
    std::snprintf(text, sizeof text, "%.17g", value);
    return text;
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

// A formula in prefix form: "+ p 3 n 1.5" is parameter 3 plus 1.5.
void write(std::string& out, const ExprNode& node) {
    switch (node.kind) {
    case Kind::Number: out += " n " + number(node.number); return;
    case Kind::Parameter: out += " p " + std::to_string(node.parameter.value); return;
    case Kind::Add: out += " +"; break;
    case Kind::Subtract: out += " -"; break;
    case Kind::Multiply: out += " *"; break;
    case Kind::Divide: out += " /"; break;
    case Kind::Negate: out += " neg"; break;
    case Kind::Min: out += " min"; break;
    case Kind::Max: out += " max"; break;
    case Kind::Abs: out += " abs"; break;
    }
    if (node.a) write(out, *node.a);
    if (node.b) write(out, *node.b);
}
void write(std::string& out, const Expr& expr) { write(out, ExprAccess::node(expr)); }

void writeVia(std::string& out, const std::vector<OpId>& via) {
    out += " " + std::to_string(via.size());
    for (OpId op : via) out += " " + std::to_string(op.value);
}
void write(std::string& out, const EdgeRef& edge) {
    out += " e " + std::to_string(edge.op.value) + " " + std::to_string(edge.role);
    writeVia(out, edge.via);
}
void write(std::string& out, const Reference& reference) {
    if (const EdgeRef* edge = std::get_if<EdgeRef>(&reference)) {
        write(out, *edge);
    } else if (const CornerRef* corner = std::get_if<CornerRef>(&reference)) {
        out += " c " + std::to_string(corner->op.value) + " " + std::to_string(corner->role);
        writeVia(out, corner->via);
    } else if (const LineRef* line = std::get_if<LineRef>(&reference)) {
        out += " l " + std::to_string(line->op.value);
    } else {
        out += " P " + std::to_string(std::get<PanelId>(reference).value);
    }
}

// What an operation holds besides formulas and references: one number each.
std::vector<long long> extras(const OpData& data) {
    if (const Instance* instance = std::get_if<Instance>(&data)) return {instance->mirrored ? 1 : 0};
    if (const Offcut* offcut = std::get_if<Offcut>(&data)) return {static_cast<long long>(OpId(offcut->of).value)};
    if (const Cut* cut = std::get_if<Cut>(&data)) return {cut->keep == Cut::Side::Right ? 1 : 0};
    if (const Mirror* mirror = std::get_if<Mirror>(&data)) return {mirror->appliedLast ? 1 : 0};
    return {};
}
bool setExtras(OpData& data, const std::vector<long long>& values) {
    const std::size_t wanted = extras(data).size();
    if (values.size() != wanted) return false;
    if (Instance* instance = std::get_if<Instance>(&data)) instance->mirrored = values[0] != 0;
    if (Offcut* offcut = std::get_if<Offcut>(&data)) offcut->of = OpRef<Cut>(OpId{static_cast<uint32_t>(values[0])});
    if (Cut* cut = std::get_if<Cut>(&data)) cut->keep = values[0] != 0 ? Cut::Side::Right : Cut::Side::Left;
    if (Mirror* mirror = std::get_if<Mirror>(&data)) mirror->appliedLast = values[0] != 0;
    return true;
}

// ---- Reading ----

struct Problem {
    std::string message;
};

// One line's words. A quoted word may hold spaces.
class Words {
public:
    explicit Words(std::string_view line) : line_(line) {}

    bool done() {
        skipSpaces();
        return at_ >= line_.size();
    }
    std::string word() {
        skipSpaces();
        if (at_ >= line_.size()) throw Problem{"the line ends too soon"};
        std::string out;
        if (line_[at_] == '"') {
            ++at_;
            while (true) {
                if (at_ >= line_.size()) throw Problem{"a name's closing quote is missing"};
                const char c = line_[at_++];
                if (c == '"') break;
                if (c == '\\' && at_ < line_.size()) {
                    const char next = line_[at_++];
                    out += next == 'n' ? '\n' : next;
                } else {
                    out += c;
                }
            }
            return out;
        }
        while (at_ < line_.size() && line_[at_] != ' ' && line_[at_] != '\t') out += line_[at_++];
        return out;
    }
    long long whole() {
        const std::string text = word();
        long long value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) throw Problem{"\"" + text + "\" should be a whole number"};
        return value;
    }
    uint32_t id() {
        const long long value = whole();
        if (value <= 0 || value > 4000000000LL) throw Problem{"an id is out of range"};
        return static_cast<uint32_t>(value);
    }
    std::size_t count() {
        const long long value = whole();
        if (value < 0 || value > 1000) throw Problem{"a count is out of range"};
        return static_cast<std::size_t>(value);
    }
    double real() {
        const std::string text = word();
        try {
            std::size_t used = 0;
            const double value = std::stod(text, &used);
            if (used == text.size()) return value;
        } catch (const std::exception&) {
        }
        throw Problem{"\"" + text + "\" should be a number"};
    }

private:
    void skipSpaces() {
        while (at_ < line_.size() && (line_[at_] == ' ' || line_[at_] == '\t' || line_[at_] == '\r')) ++at_;
    }
    std::string_view line_;
    std::size_t at_ = 0;
};

Expr readExpr(Words& words, int depth = 0) {
    if (depth > 200) throw Problem{"a formula is nested too deeply"};
    const std::string head = words.word();
    if (head == "n") return Expr(words.real());
    if (head == "p") return Expr(Param{words.id()});
    const auto next = [&] { return readExpr(words, depth + 1); };
    if (head == "neg") return ExprAccess::make(ExprNode{Kind::Negate, 0.0, {}, ExprAccess::share(next()), nullptr});
    if (head == "abs") return abs(next());
    Expr a = next();
    Expr b = next();
    if (head == "+") return a + b;
    if (head == "-") return a - b;
    if (head == "*") return a * b;
    if (head == "/") return a / b;
    if (head == "min") return min(a, b);
    if (head == "max") return max(a, b);
    throw Problem{"\"" + head + "\" is not part of a formula"};
}

std::vector<OpId> readVia(Words& words) {
    std::vector<OpId> via(words.count());
    for (OpId& op : via) op = OpId{words.id()};
    return via;
}
Reference readReference(Words& words) {
    const std::string kind = words.word();
    if (kind == "e" || kind == "c") {
        // An unset reference has operation 0.
        const long long op = words.whole();
        const long long role = words.whole();
        if (op < 0 || op > 4000000000LL || role < 0 || role > 65535) throw Problem{"a reference is out of range"};
        std::vector<OpId> via = readVia(words);
        if (kind == "e") return EdgeRef{OpId{static_cast<uint32_t>(op)}, static_cast<uint16_t>(role), std::move(via)};
        return CornerRef{OpId{static_cast<uint32_t>(op)}, static_cast<uint16_t>(role), std::move(via)};
    }
    const long long target = words.whole();
    if (target < 0 || target > 4000000000LL) throw Problem{"a reference is out of range"};
    if (kind == "l") return LineRef{OpId{static_cast<uint32_t>(target)}};
    if (kind == "P") return PanelId{static_cast<uint32_t>(target)};
    throw Problem{"\"" + kind + "\" is not a kind of reference"};
}
EdgeRef readEdge(Words& words) {
    const Reference reference = readReference(words);
    if (const EdgeRef* edge = std::get_if<EdgeRef>(&reference)) return *edge;
    throw Problem{"an edge was expected"};
}

const char* const firstLine = "clothcad 1";

} // namespace

std::string toText(const Document& doc) {
    std::string out = std::string(firstLine) + "\n";
    out += "next " + std::to_string(DocumentAccess::nextId(doc)) + "\n";
    for (Param id : doc.parameters()) {
        const Parameter& parameter = doc.get(id);
        out += "parameter " + std::to_string(id.value) + " " + quoted(parameter.name);
        write(out, parameter.value);
        // A length says nothing, as in files before 0.1.1.
        if (parameter.unit != Unit::Length) out += " unit " + std::string(unitNames[static_cast<std::size_t>(parameter.unit)]);
        out += "\n";
    }
    for (PanelId id : doc.panels()) {
        const Panel& panel = doc.get(id);
        out += "panel " + std::to_string(id.value) + " " + quoted(panel.name) + " " + (panel.construction ? "1" : "0") + " " +
               number(panel.x) + " " + number(panel.y) + " " + number(panel.angle) + "\n";
        if (const Arrangement& in = panel.arrangement; in.placed)
            out += "arrange " + number(in.x) + " " + number(in.y) + " " + number(in.z) + " " + number(in.turn) + " " + number(in.tilt) + " " +
                   number(in.roll) + " " + number(in.wrap) + (in.anchor ? " " + number(in.anchor->x) + " " + number(in.anchor->y) : std::string()) + "\n";
        for (OpId op : doc.operations(id)) {
            const OpData& data = doc.get(op);
            out += "op " + std::to_string(op.value) + " " + std::string(typeNames[data.index()]) + " " + (doc.enabled(op) ? "1" : "0");
            const std::vector<Field> formulas = fields(data);
            out += " " + std::to_string(formulas.size());
            for (const Field& field : formulas) write(out, field.value);
            const std::vector<ReferenceField> slots = references(data);
            out += " " + std::to_string(slots.size());
            for (const ReferenceField& slot : slots) write(out, slot.value);
            const std::vector<long long> more = extras(data);
            out += " " + std::to_string(more.size());
            for (long long value : more) out += " " + std::to_string(value);
            out += "\n";
        }
    }
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        out += "seam " + std::to_string(id.value) + " " + std::to_string(seam.a.panel.value);
        write(out, seam.a.edge);
        out += " " + std::to_string(seam.b.panel.value);
        write(out, seam.b.edge);
        out += std::string(" ") + (seam.reversed ? "1" : "0") + "\n";
    }
    return out;
}

std::variant<Document, FileError> fromText(std::string_view text) {
    Document doc;
    std::vector<uint32_t> seen;
    const auto fresh = [&](uint32_t id) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) throw Problem{"the id " + std::to_string(id) + " is used twice"};
        seen.push_back(id);
        return id;
    };
    std::optional<PanelId> panel;  // the panel the "op" lines belong to
    std::size_t lineNumber = 0;
    bool started = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        try {
            Words words(line);
            if (words.done()) continue;
            const std::string what = words.word();
            if (!started) {
                if (what != "clothcad") throw Problem{"this is not a cloth CAD file"};
                if (words.whole() != 1) throw Problem{"this file was written by a newer version"};
                started = true;
                continue;
            }
            if (what == "next") {
                const uint32_t next = words.id();
                if (next > 1) DocumentAccess::used(doc, next - 1);
            } else if (what == "parameter") {
                const Param id{fresh(words.id())};
                std::string name = words.word();
                Parameter parameter{std::move(name), readExpr(words)};
                if (!words.done()) {
                    if (words.word() != "unit") throw Problem{"a parameter has something after its value that is not its unit"};
                    const std::string unit = words.word();
                    const auto known = std::find(unitNames.begin(), unitNames.end(), unit);
                    if (known == unitNames.end()) throw Problem{"\"" + unit + "\" is not a unit this version knows"};
                    parameter.unit = static_cast<Unit>(known - unitNames.begin());
                }
                DocumentAccess::restore(doc, id, std::move(parameter));
            } else if (what == "panel") {
                const PanelId id{fresh(words.id())};
                Panel description;
                description.name = words.word();
                description.construction = words.whole() != 0;
                description.x = words.real();
                description.y = words.real();
                description.angle = words.real();
                DocumentAccess::restore(doc, id, std::move(description));
                panel = id;
            } else if (what == "arrange") {
                if (!panel) throw Problem{"an arrangement comes before any panel"};
                Arrangement in;
                in.placed = true;
                in.x = words.real();
                in.y = words.real();
                in.z = words.real();
                in.turn = words.real();
                in.tilt = words.real();
                in.roll = words.real();
                in.wrap = words.real();
                if (!words.done()) {  // files before 0.1.1 have no anchor
                    const double x = words.real();
                    in.anchor = Vec2{x, words.real()};
                }
                doc.arrange(*panel, in);
            } else if (what == "op") {
                if (!panel) throw Problem{"an operation comes before any panel"};
                const OpId id{fresh(words.id())};
                const std::string type = words.word();
                std::optional<OpData> data = emptyOperation(type);
                if (!data) throw Problem{"\"" + type + "\" is not a kind of operation this version knows"};
                const bool enabled = words.whole() != 0;
                const std::size_t formulas = words.count();
                if (formulas != fields(*data).size()) throw Problem{"a " + type + " has a different number of values"};
                for (std::size_t i = 0; i < formulas; ++i) *fieldAt(*data, i) = readExpr(words);
                const std::size_t slots = words.count();
                if (slots != references(*data).size()) throw Problem{"a " + type + " has a different number of references"};
                for (std::size_t i = 0; i < slots; ++i)
                    if (setReferenceAt(*data, i, readReference(words)) != ReferenceChange::Done)
                        throw Problem{"a reference of this " + type + " is of the wrong kind"};
                std::vector<long long> more(words.count());
                for (long long& value : more) value = words.whole();
                if (!setExtras(*data, more)) throw Problem{"a " + type + " has a different number of settings"};
                DocumentAccess::restore(doc, *panel, id, std::move(*data), enabled);
            } else if (what == "seam") {
                const SeamId id{fresh(words.id())};
                Seam seam;
                seam.a.panel = PanelId{words.id()};
                seam.a.edge = readEdge(words);
                seam.b.panel = PanelId{words.id()};
                seam.b.edge = readEdge(words);
                seam.reversed = words.whole() != 0;
                if (!doc.contains(seam.a.panel) || !doc.contains(seam.b.panel)) throw Problem{"a seam names a panel that is not in the file"};
                DocumentAccess::restore(doc, id, std::move(seam));
            } else {
                throw Problem{"\"" + what + "\" is not something this version knows"};
            }
            if (!words.done()) throw Problem{"there is more on the line than expected"};
        } catch (const Problem& problem) {
            return FileError{lineNumber, problem.message};
        }
    }
    if (!started) return FileError{0, "the file is empty"};
    return doc;
}

std::string save(const Document& doc, const std::string& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return "could not open \"" + path + "\" for writing";
    file << toText(doc);
    file.flush();
    if (!file) return "could not write \"" + path + "\"";
    return {};
}

std::variant<Document, FileError> open(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return FileError{0, "could not open \"" + path + "\""};
    std::ostringstream text;
    text << file.rdbuf();
    return fromText(text.str());
}

} // namespace clothcad
