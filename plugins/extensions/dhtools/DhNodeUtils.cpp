/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhNodeUtils.h"

#include <kis_node.h>
#include <kis_node_facade.h>
#include <kis_image.h>
#include <KisViewManager.h>
#include <kis_node_manager.h>

namespace DhNodeUtils
{

KisNodeList flattenVisualNodeOrder(KisNodeSP node)
{
    KisNodeList result;
    if (!node) return result;

    // childCount() is unsigned, so take the count out first -- decrementing an
    // unsigned zero is how you get a four-billion-iteration loop.
    const int count = static_cast<int>(node->childCount());
    for (int i = count - 1; i >= 0; --i) {
        KisNodeSP child = node->at(static_cast<quint32>(i));
        if (!child) continue;

        result.append(child);
        result.append(flattenVisualNodeOrder(child));
    }

    return result;
}

KisNodeSP activateRelativeNode(KisViewManager *view, int offset)
{
    if (!view || !view->image()) return KisNodeSP();

    KisNodeManager *nodeManager = view->nodeManager();
    if (!nodeManager) return KisNodeSP();

    KisNodeSP active = nodeManager->activeNode();
    if (!active) return KisNodeSP();

    const KisNodeList flat = flattenVisualNodeOrder(view->image()->root());

    const int index = flat.indexOf(active);
    if (index < 0) return KisNodeSP();

    const int target = index + offset;
    if (target < 0 || target >= flat.size()) return KisNodeSP();

    KisNodeSP node = flat.at(target);
    nodeManager->slotNonUiActivatedNode(node);
    return node;
}

}
