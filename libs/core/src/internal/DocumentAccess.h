#pragma once
// Internal: putting things into a Document under ids that were chosen
// earlier, which only reading a file needs. Included only by core/*.cpp.

#include <algorithm>
#include <utility>

#include "clothcad/core/Document.h"

namespace clothcad {

struct DocumentAccess {
    static uint32_t nextId(const Document& doc) { return doc.nextId_; }
    static void used(Document& doc, uint32_t id) { doc.nextId_ = std::max(doc.nextId_, id + 1); }

    static void restore(Document& doc, Param id, Parameter parameter) {
        doc.parameters_.push_back({id, std::move(parameter)});
        used(doc, id.value);
    }
    static void restore(Document& doc, PanelId id, Panel panel) {
        doc.panels_.push_back({id, std::move(panel), {}});
        used(doc, id.value);
    }
    // The panel must have been restored already.
    static void restore(Document& doc, PanelId panel, OpId id, OpData data, bool enabled) {
        doc.entry(panel).operations.push_back({id, std::move(data), enabled});
        used(doc, id.value);
    }
    static void restore(Document& doc, SeamId id, Seam seam) {
        doc.seams_.push_back({id, std::move(seam)});
        used(doc, id.value);
    }
};

} // namespace clothcad
