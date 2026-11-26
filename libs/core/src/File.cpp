#include "clothcad/core/File.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
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
    "SetLength",
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
static_assert(std::is_same_v<std::variant_alternative_t<14, OpData>, SetLength>);

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

// The shortest text that reads back as the same number: an avatar has tens of
// thousands of them.
std::string shortest(double value) {
    char text[32];
    const auto [end, error] = std::to_chars(text, text + sizeof text, value);
    return std::string(text, end);
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
    if (const SetLength* set = std::get_if<SetLength>(&data)) return {static_cast<long long>(set->keeping)};
    return {};
}
bool setExtras(OpData& data, const std::vector<long long>& values) {
    const std::size_t wanted = extras(data).size();
    if (values.size() != wanted) return false;
    if (Instance* instance = std::get_if<Instance>(&data)) instance->mirrored = values[0] != 0;
    if (Offcut* offcut = std::get_if<Offcut>(&data)) offcut->of = OpRef<Cut>(OpId{static_cast<uint32_t>(values[0])});
    if (Cut* cut = std::get_if<Cut>(&data)) cut->keep = values[0] != 0 ? Cut::Side::Right : Cut::Side::Left;
    if (Mirror* mirror = std::get_if<Mirror>(&data)) mirror->appliedLast = values[0] != 0;
    if (SetLength* set = std::get_if<SetLength>(&data)) {
        if (values[0] < 0 || values[0] > static_cast<long long>(SetLength::Keeping::End)) return false;  // not a way of keeping we know
        set->keeping = static_cast<SetLength::Keeping>(values[0]);
    }
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
    std::size_t count(long long most = 1000) {
        const long long value = whole();
        if (value < 0 || value > most) throw Problem{"a count is out of range"};
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
constexpr long long mostOfAnAvatar = 10'000'000;  // points, or triangles

// The avatar's lines come last: its name and how many points and triangles
// follow, then a line for each.
void write(std::string& out, const Avatar& avatar) {
    out += "avatar " + quoted(avatar.name) + " " + std::to_string(avatar.points.size()) + " " + std::to_string(avatar.triangles.size()) + "\n";
    for (const Point3& p : avatar.points) out += "v " + shortest(p.x) + " " + shortest(p.y) + " " + shortest(p.z) + "\n";
    for (const std::array<uint32_t, 3>& t : avatar.triangles) out += "t " + std::to_string(t[0]) + " " + std::to_string(t[1]) + " " + std::to_string(t[2]) + "\n";
}

// An avatar as far as it has been read.
struct ReadingAvatar {
    std::shared_ptr<Avatar> avatar;
    std::size_t points = 0, triangles = 0;  // how many its first line announces
    std::size_t line = 0;                   // that line
};

// How the scene was left: a line for each thing frozen or hidden, then the
// drape: how many panels' cloth follows, and for each its anchor, how many
// particles (flat u v, space x y z) and triangles, then a line for each.
void write(std::string& out, const SceneState& state) {
    for (PanelId panel : state.frozen) out += "frozen " + std::to_string(panel.value) + "\n";
    for (PanelId panel : state.hidden) out += "hidden " + std::to_string(panel.value) + "\n";
    for (SeamId seam : state.hiddenSeams) out += "hiddenseam " + std::to_string(seam.value) + "\n";
    if (state.bodyHidden) out += "body hidden\n";
    if (state.bodySeeThrough) out += "body seethrough\n";
    if (!state.drape) return;
    out += "drape " + shortest(state.drape->time) + " " + (state.drape->sewing ? "1" : "0") + " " + std::to_string(state.drape->panels.size()) + "\n";
    for (const DrapedPanel& cloth : state.drape->panels) {
        const std::size_t particles = std::min(cloth.flat.size(), cloth.at.size());
        out += "cloth " + std::to_string(cloth.panel.value) + " " + shortest(cloth.anchor.x) + " " + shortest(cloth.anchor.y) + " " +
               std::to_string(particles) + " " + std::to_string(cloth.triangles.size()) + "\n";
        for (std::size_t i = 0; i < particles; ++i) {
            const Vec2& flat = cloth.flat[i];
            const Point3& at = cloth.at[i];
            out += "d " + shortest(flat.x) + " " + shortest(flat.y) + " " + shortest(at.x) + " " + shortest(at.y) + " " + shortest(at.z) + "\n";
        }
        for (const std::array<uint32_t, 3>& t : cloth.triangles) out += "f " + std::to_string(t[0]) + " " + std::to_string(t[1]) + " " + std::to_string(t[2]) + "\n";
    }
}

double finite(Words& words) {
    const double value = words.real();
    if (!std::isfinite(value)) throw Problem{"a number here is not a number"};
    return value;
}

// Three of the first `count` particles or points.
std::array<uint32_t, 3> readTriangle(Words& words, std::size_t count, const char* otherwise) {
    std::array<uint32_t, 3> triangle;
    for (uint32_t& corner : triangle) {
        const long long point = words.whole();
        if (point < 0 || point >= static_cast<long long>(count)) throw Problem{otherwise};
        corner = static_cast<uint32_t>(point);
    }
    return triangle;
}

// A drape as far as it has been read.
struct ReadingDrape {
    std::size_t panels = 0;                 // how many the drape's line announces
    std::size_t particles = 0, triangles = 0;  // how many the latest cloth line announces
    std::size_t line = 0;                   // the drape's line

    // Whether everything that was announced has come.
    bool whole(const SavedDrape& drape) const { return drape.panels.size() == panels && latestWhole(drape); }
    bool latestWhole(const SavedDrape& drape) const {
        return drape.panels.empty() || (drape.panels.back().at.size() == particles && drape.panels.back().triangles.size() == triangles);
    }
};

} // namespace

std::filesystem::path filePath(std::string_view utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string toText(const Document& doc, const SceneState& state) {
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
               number(panel.x) + " " + number(panel.y) + " " + number(panel.angle) +
               (panel.particleDistance ? " " + number(*panel.particleDistance) : std::string()) + "\n";
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
    write(out, state);
    if (doc.avatar()) write(out, *doc.avatar());
    return out;
}

std::variant<SavedScene, FileError> fromText(std::string_view text) {
    Document doc;
    SceneState state;
    ReadingDrape drape;
    std::vector<uint32_t> seen;
    const auto fresh = [&](uint32_t id) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) throw Problem{"the id " + std::to_string(id) + " is used twice"};
        seen.push_back(id);
        return id;
    };
    std::optional<PanelId> panel;  // the panel the "op" lines belong to
    ReadingAvatar body;
    std::size_t lineNumber = 0;
    bool started = false;
    // A panel or seam the scene's lines name: they come after the pattern's.
    const auto knownPanel = [&](Words& words) {
        const PanelId id{words.id()};
        if (!doc.contains(id)) throw Problem{"this names a panel that is not in the file"};
        return id;
    };
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
                if (!words.done()) description.particleDistance = words.real();  // older files have none
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
                if (!setExtras(*data, more)) throw Problem{"a " + type + " has a different number of settings, or one that is not allowed"};
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
            } else if (what == "avatar") {
                if (body.avatar) throw Problem{"there is a second avatar"};
                body.avatar = std::make_shared<Avatar>();
                body.avatar->name = words.word();
                body.points = words.count(mostOfAnAvatar);
                body.triangles = words.count(mostOfAnAvatar);
                body.line = lineNumber;
            } else if (what == "v") {
                if (!body.avatar || body.avatar->points.size() == body.points) throw Problem{"there is a point more than the avatar announces"};
                Point3 p;
                p.x = words.real();
                p.y = words.real();
                p.z = words.real();
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) throw Problem{"a point of the avatar is not a number"};
                body.avatar->points.push_back(p);
            } else if (what == "t") {
                if (!body.avatar || body.avatar->triangles.size() == body.triangles) throw Problem{"there is a triangle more than the avatar announces"};
                body.avatar->triangles.push_back(readTriangle(words, body.points, "a triangle of the avatar names a point that is not there"));
            } else if (what == "frozen") {
                state.frozen.push_back(knownPanel(words));
            } else if (what == "hidden") {
                state.hidden.push_back(knownPanel(words));
            } else if (what == "hiddenseam") {
                const SeamId id{words.id()};
                if (!doc.contains(id)) throw Problem{"this names a seam that is not in the file"};
                state.hiddenSeams.push_back(id);
            } else if (what == "body") {
                const std::string how = words.word();
                if (how == "hidden") state.bodyHidden = true;
                else if (how == "seethrough") state.bodySeeThrough = true;
                else throw Problem{"\"" + how + "\" is not a way of showing the body this version knows"};
            } else if (what == "drape") {
                if (state.drape) throw Problem{"there is a second drape"};
                state.drape.emplace();
                state.drape->time = finite(words);
                state.drape->sewing = words.whole() != 0;
                drape.panels = words.count(mostOfAnAvatar);
                drape.line = lineNumber;
            } else if (what == "cloth") {
                if (!state.drape || state.drape->panels.size() == drape.panels) throw Problem{"there is more cloth than the drape announces"};
                if (!drape.latestWhole(*state.drape)) throw Problem{"the cloth before this one is cut short"};
                DrapedPanel cloth;
                cloth.panel = knownPanel(words);
                cloth.anchor.x = finite(words);
                cloth.anchor.y = finite(words);
                drape.particles = words.count(mostOfAnAvatar);
                drape.triangles = words.count(mostOfAnAvatar);
                state.drape->panels.push_back(std::move(cloth));
            } else if (what == "d") {
                if (!state.drape || state.drape->panels.empty() || state.drape->panels.back().at.size() == drape.particles)
                    throw Problem{"there is a particle more than the cloth announces"};
                Vec2 flat;
                flat.x = finite(words);
                flat.y = finite(words);
                Point3 at;
                at.x = finite(words);
                at.y = finite(words);
                at.z = finite(words);
                state.drape->panels.back().flat.push_back(flat);
                state.drape->panels.back().at.push_back(at);
            } else if (what == "f") {
                if (!state.drape || state.drape->panels.empty() || state.drape->panels.back().triangles.size() == drape.triangles)
                    throw Problem{"there is a triangle more than the cloth announces"};
                state.drape->panels.back().triangles.push_back(readTriangle(words, drape.particles, "a triangle of the cloth names a particle that is not there"));
            } else {
                throw Problem{"\"" + what + "\" is not something this version knows"};
            }
            if (!words.done()) throw Problem{"there is more on the line than expected"};
        } catch (const Problem& problem) {
            return FileError{lineNumber, problem.message};
        }
    }
    if (!started) return FileError{0, "the file is empty"};
    if (state.drape && !drape.whole(*state.drape)) return FileError{drape.line, "the draped cloth is cut short: the file is incomplete"};
    if (body.avatar) {
        if (body.avatar->points.size() != body.points || body.avatar->triangles.size() != body.triangles)
            return FileError{body.line, "the avatar is cut short: the file is incomplete"};
        doc.setAvatar(std::move(body.avatar));
    }
    return SavedScene{std::move(doc), std::move(state)};
}

std::string save(const Document& doc, const std::string& path, const SceneState& state) {
    const std::filesystem::path target = filePath(path);
    std::filesystem::path beside = target;
    beside += ".saving";
    std::error_code ignored;
    {
        std::ofstream file(beside, std::ios::binary | std::ios::trunc);
        if (!file) return "could not open \"" + path + "\" for writing";
        file << toText(doc, state);
        file.close();
        if (!file) {
            std::filesystem::remove(beside, ignored);
            return "could not write \"" + path + "\"";
        }
    }
    std::error_code failed;
    std::filesystem::rename(beside, target, failed);  // takes the old file's place in one step
    if (failed) {
        std::filesystem::remove(beside, ignored);
        return "could not replace \"" + path + "\": it may be read-only or open in another program";
    }
    return {};
}

std::variant<SavedScene, FileError> open(const std::string& path) {
    std::ifstream file(filePath(path), std::ios::binary);
    if (!file) return FileError{0, "could not open \"" + path + "\""};
    std::ostringstream text;
    text << file.rdbuf();
    return fromText(text.str());
}

} // namespace clothcad
