/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DHNODEUTILS_H
#define DHNODEUTILS_H

#include <kis_types.h>

class KisViewManager;

/**
 * The layer-stack walk shared by "Layer Above/Below" and Layer Cycle Mode.
 *
 * Krita's own activateNextLayer/activatePreviousLayer walk siblings and only
 * descend into groups on the way past them, which means a mask with no sibling
 * masks is a dead end -- press the shortcut on a filter mask and nothing
 * happens. These walk the whole tree flattened into exactly the order the
 * Layers docker paints it, masks and nested groups included, so every entry
 * you can see is an entry you can step onto.
 */
namespace DhNodeUtils
{
    /**
     * Depth-first flatten of the node tree in the Layers docker's visual
     * top-to-bottom order. KisNode indexes children bottom-to-top, so we walk
     * each level in reverse and recurse immediately after each item.
     *
     * @param node subtree root; pass the image root node
     */
    KisNodeList flattenVisualNodeOrder(KisNodeSP node);

    /**
     * Move the active node by @p offset positions through the flattened stack.
     * The list runs top-to-bottom, so -1 is "above" and +1 is "below".
     *
     * @return the node that ended up active, or 0 if we could not move
     */
    KisNodeSP activateRelativeNode(KisViewManager *view, int offset);
}

#endif // DHNODEUTILS_H
