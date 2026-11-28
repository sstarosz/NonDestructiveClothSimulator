#include "twintailor/session/Commands.h"

#include <optional>

#include "twintailor/core/Text.h"
#include "Corners.h"

namespace twintailor::session {

namespace {

bool parameterNamed(const Document& doc, const std::string& name, std::optional<Param> but = std::nullopt) {
    for (Param other : doc.parameters())
        if (!(but && other == *but) && doc.get(other).name == name) return true;
    return false;
}

// Why `name` can't be the name of a parameter (`self`, if it exists already); empty if it can.
std::string nameProblem(const Document& doc, const std::string& name, std::optional<Param> self = std::nullopt) {
    if (!canBeTypedInFormulas(name))
        return "'" + name + "' can't be a parameter name. Use letters, digits and underscores, starting with a letter; "
               "not pi, min, max, abs, clamp, cm, mm or deg.";
    if (parameterNamed(doc, name, self)) return "There is already a parameter named '" + name + "'. Choose another name.";
    return {};
}

} // namespace

std::expected<Nothing, std::string> RenamePanel::apply(Document& doc, const Result&) const {
    if (name.find_first_not_of(" \t") == std::string::npos) return std::unexpected("A panel needs a name. Type one, or press Escape to keep the old one.");
    doc.rename(panel, name);
    return Nothing{};
}

std::expected<Nothing, std::string> SetConstruction::apply(Document& doc, const Result&) const {
    doc.setConstruction(panel, construction);
    return Nothing{};
}

std::expected<Nothing, std::string> SetParticleDistance::apply(Document& doc, const Result&) const {
    const std::optional<double> cm = asNumber(distance);
    if (!cm || *cm < finestParticleDistance || *cm > coarsestParticleDistance)
        return std::unexpected("Between " + formatValue(finestParticleDistance, Unit::Length) + " and " + formatValue(coarsestParticleDistance, Unit::Length) + ".");
    doc.setParticleDistance(panel, *cm);
    return Nothing{};
}

std::expected<AddParameter::Done, std::string> AddParameter::apply(Document& doc, const Result&) const {
    std::string chosen = name;
    if (chosen.empty()) {
        int number = 1;
        while (parameterNamed(doc, "p" + std::to_string(number))) ++number;
        chosen = "p" + std::to_string(number);
    } else if (std::string why = nameProblem(doc, chosen); !why.empty()) {
        return std::unexpected(std::move(why));
    }
    return Done{doc.add(Parameter{.name = chosen, .value = value, .unit = unit})};
}

std::expected<Nothing, std::string> RenameParameter::apply(Document& doc, const Result&) const {
    static_cast<void>(doc.get(parameter));  // it is there
    if (std::string why = nameProblem(doc, name, parameter); !why.empty()) return std::unexpected(std::move(why));
    doc.rename(parameter, name);
    return Nothing{};
}

std::expected<Nothing, std::string> RemoveParameter::apply(Document& doc, const Result&) const {
    doc.remove(parameter);
    return Nothing{};
}

std::expected<Nothing, std::string> SetAvatar::apply(Document& doc, const Result&) const {
    doc.setAvatar(avatar);
    return Nothing{};
}

} // namespace twintailor::session
