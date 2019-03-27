/*
 * scriptedtool.cpp
 * Copyright 2019, Thorbjørn Lindeijer <bjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "scriptedtool.h"

#include "editablemap.h"
#include "mapdocument.h"
#include "pluginmanager.h"
#include "scriptmanager.h"
#include "tile.h"
#include "tilesetdocument.h"

#include <QJSEngine>
#include <QKeyEvent>

namespace Tiled {

ScriptedTool::ScriptedTool(QJSValue object, QObject *parent)
    : AbstractTileTool(QStringLiteral("<unnamed tool>"), QIcon(), QKeySequence(), nullptr, parent)
    , mScriptObject(object)
{
    const QJSValue nameProperty = object.property(QStringLiteral("name"));
    if (nameProperty.isString()) {
        const QString name = nameProperty.toString();
        if (!name.isEmpty())
            setName(name);
    }

    PluginManager::addObject(this);
}

ScriptedTool::~ScriptedTool()
{
    PluginManager::removeObject(this);
}

EditableMap *ScriptedTool::editableMap() const
{
    return mapDocument() ? static_cast<EditableMap*>(mapDocument()->editable())
                         : nullptr;
}

EditableTile *ScriptedTool::editableTile() const
{
    if (Tile *t = tile()) {
        auto tileset = t->tileset()->sharedPointer();

        if (auto tilesetDocument = TilesetDocument::findDocumentForTileset(tileset)) {
            EditableTileset *editable = tilesetDocument->editable();
            return editable->tile(t->id());
        }
    }

    return nullptr;
}

void ScriptedTool::activate(MapScene *scene)
{
    AbstractTileTool::activate(scene);
    mScene = scene;
    call(QStringLiteral("activated"));
}

void ScriptedTool::deactivate(MapScene *scene)
{
    AbstractTileTool::deactivate(scene);
    call(QStringLiteral("deactivated"));
    mScene = nullptr;
}

void ScriptedTool::keyPressed(QKeyEvent *keyEvent)
{
    QJSValueList args;
    args.append(keyEvent->key());
    args.append(static_cast<int>(keyEvent->modifiers()));

    call(QStringLiteral("keyPressed"), args);
}

void ScriptedTool::mouseEntered()
{
    AbstractTileTool::mouseEntered();
    call(QStringLiteral("mouseEntered"));
}

void ScriptedTool::mouseLeft()
{
    AbstractTileTool::mouseLeft();
    call(QStringLiteral("mouseEntered"));
}

void ScriptedTool::mouseMoved(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    AbstractTileTool::mouseMoved(pos, modifiers);

    QJSValueList args;
    args.append(pos.x());
    args.append(pos.y());
    args.append(static_cast<int>(modifiers));

    call(QStringLiteral("mouseMoved"), args);
}

void ScriptedTool::mousePressed(QGraphicsSceneMouseEvent *event)
{
    QJSValueList args;
    args.append(event->button());
    args.append(event->pos().x());
    args.append(event->pos().y());
    args.append(static_cast<int>(event->modifiers()));

    call(QStringLiteral("mousePressed"), args);
}

void ScriptedTool::mouseReleased(QGraphicsSceneMouseEvent *event)
{
    QJSValueList args;
    args.append(event->button());
    args.append(event->pos().x());
    args.append(event->pos().y());
    args.append(static_cast<int>(event->modifiers()));

    call(QStringLiteral("mouseReleased"), args);
}

void ScriptedTool::mouseDoubleClicked(QGraphicsSceneMouseEvent *event)
{
    QJSValueList args;
    args.append(event->button());
    args.append(event->pos().x());
    args.append(event->pos().y());
    args.append(static_cast<int>(event->modifiers()));

    if (!call(QStringLiteral("mouseDoubleClicked"), args))
        mousePressed(event);
}

void ScriptedTool::modifiersChanged(Qt::KeyboardModifiers modifiers)
{
    QJSValueList args;
    args.append(static_cast<int>(modifiers));

    call(QStringLiteral("modifiersChanged"), args);
}

void ScriptedTool::languageChanged()
{
    call(QStringLiteral("languageChanged"));
}

void ScriptedTool::populateToolBar(QToolBar *)
{
    // todo: consider how to support this (is it enough to have a list of
    // actions?)
}

bool ScriptedTool::validateToolObject(QJSValue value)
{
    const QJSValue nameProperty = value.property(QStringLiteral("name"));

    if (!nameProperty.isString()) {
        ScriptManager::instance().throwError(tr("Invalid tool object (requires string 'name' property)"));
        return false;
    }

    return true;
}

void ScriptedTool::mapDocumentChanged(MapDocument *oldDocument,
                                      MapDocument *newDocument)
{
    AbstractTileTool::mapDocumentChanged(oldDocument, newDocument);

    auto engine = ScriptManager::instance().engine();

    QJSValueList args;
    args.append(oldDocument ? engine->newQObject(oldDocument->editable()) : QJSValue::NullValue);
    args.append(newDocument ? engine->newQObject(newDocument->editable()) : QJSValue::NullValue);

    call(QStringLiteral("mapChanged"), args);
}

void ScriptedTool::tilePositionChanged(const QPoint &tilePos)
{
    QJSValueList args;
    args.append(tilePos.x());
    args.append(tilePos.y());

    call(QStringLiteral("tilePositionChanged"), args);
}

void ScriptedTool::updateEnabledState()
{
    if (!call(QStringLiteral("updateEnabledState"))) {
        // Skipping AbstractTileTool since we do not want the enabled state to
        // automatically depend on any selected tile layers.
        AbstractTool::updateEnabledState();
    }
    updateBrushVisibility();
}

bool ScriptedTool::call(const QString &methodName, const QJSValueList &args)
{
    QJSValue method = mScriptObject.property(methodName);
    if (method.isCallable()) {
        auto &scriptManager = ScriptManager::instance();
        auto self = scriptManager.engine()->newQObject(this);

        // Make whatever members were included in the original object available
        // through the instance pointed to by 'this'.
        self.setPrototype(mScriptObject);

        QJSValue result = method.callWithInstance(self, args);
        scriptManager.checkError(result);

        return true;
    }
    return false;
}

} // namespace Tiled