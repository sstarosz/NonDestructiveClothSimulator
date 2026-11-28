#include "twintailor/session/Commands.h"

#include <algorithm>

namespace twintailor::session {

std::expected<AddSeam::Done, std::string> AddSeam::apply(Document& doc, const Result&) const {
    static_cast<void>(doc.get(seam.a.panel));  // both are there
    static_cast<void>(doc.get(seam.b.panel));
    for (SeamId id : doc.seams()) {
        const Seam& sewn = doc.get(id);
        if ((sewn.a == seam.a && sewn.b == seam.b) || (sewn.a == seam.b && sewn.b == seam.a))
            return std::unexpected("These two edges are already sewn together.");
    }
    return Done{doc.add(seam)};
}

std::expected<Nothing, std::string> SetSeamReversed::apply(Document& doc, const Result&) const {
    Seam changed = doc.get(seam);
    changed.reversed = reversed;
    doc.set(seam, changed);
    return Nothing{};
}

std::expected<Nothing, std::string> RemoveSeam::apply(Document& doc, const Result&) const {
    doc.remove(seam);
    return Nothing{};
}

std::expected<SewInstances::Done, std::string> SewInstances::apply(Document& doc, const Result&) const {
    std::vector<PanelId> followers;
    for (PanelId panel : instances) {
        static_cast<void>(doc.get(panel));  // it is there
        if (doc.instanceOf(panel)) followers.push_back(panel);
    }
    if (followers.empty()) return std::unexpected("None of these panels follows another: only an instance can be sewn as its source is.");
    std::size_t added = 0;
    for (PanelId panel : followers) added += doc.sewAsSource(panel).size();
    if (added > 0) return Done{added};

    const std::vector<SeamId> seams = doc.seams();
    const bool sourcesSewn = std::any_of(followers.begin(), followers.end(), [&](PanelId panel) {
        const PanelId source = doc.instanceOf(panel)->source;
        return std::any_of(seams.begin(), seams.end(), [&](SeamId id) { return doc.get(id).a.panel == source || doc.get(id).b.panel == source; });
    });
    if (!sourcesSewn)
        return std::unexpected("Nothing to sew: the selected instances follow panels without seams. Sew those panels first, then try again.");
    return std::unexpected("Nothing to add: each seam is there already, or the panel on its other side has no free instance of the same kind "
                           "(plain or mirrored). Make that instance, or use the Sew tool.");
}

} // namespace twintailor::session
