#include "twintailor/session/Commands.h"

#include <algorithm>

namespace twintailor::session {

std::expected<Added, std::string> AddLayer::apply(Document& doc, const Result&) const {
    static_cast<void>(doc.get(panel));  // it is there
    return addOffered(doc, panel, offer);
}

std::expected<Nothing, std::string> RemoveLayer::apply(Document& doc, const Result&) const {
    doc.remove(layer);
    return Nothing{};
}

std::expected<Nothing, std::string> SwitchLayer::apply(Document& doc, const Result&) const {
    doc.setEnabled(layer, on);
    return Nothing{};
}

std::expected<Nothing, std::string> MoveLayer::apply(Document& doc, const Result&) const {
    const std::vector<OpId> layers = doc.operations(doc.panelOf(layer));
    const auto from = static_cast<std::size_t>(std::find(layers.begin(), layers.end(), layer) - layers.begin());
    if (from == 0 || to == 0) return std::unexpected("the starting shape stays first, and nothing goes before it");
    if (to >= layers.size()) return std::unexpected("there is no such place among the layers");
    if (to == from) return std::unexpected("the layer is there already");
    doc.move(layer, to);
    return Nothing{};
}

std::expected<Nothing, std::string> SetReference::apply(Document& doc, const Result&) const {
    doc.set(layer, reference, value);
    return Nothing{};
}

std::expected<Nothing, std::string> SetCutSide::apply(Document& doc, const Result&) const {
    doc.set(layer, &Cut::keep, keep);
    return Nothing{};
}

std::expected<Nothing, std::string> SetMirrorLast::apply(Document& doc, const Result&) const {
    doc.set(layer, &Mirror::appliedLast, last);
    return Nothing{};
}

std::expected<Nothing, std::string> SetInstanceMirrored::apply(Document& doc, const Result&) const {
    doc.set(layer, &Instance::mirrored, mirrored);
    return Nothing{};
}

std::expected<BakePanel::Done, std::string> BakePanel::apply(Document& doc, const Result&) const {
    static_cast<void>(doc.get(panel));  // it is there
    const std::size_t replaced = bake(doc, panel);
    if (replaced == 0) return std::unexpected("Nothing to bake: the panel has no formulas.");
    return Done{replaced};
}

} // namespace twintailor::session
