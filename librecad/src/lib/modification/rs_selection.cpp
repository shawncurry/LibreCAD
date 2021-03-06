/****************************************************************************
**
** This file is part of the LibreCAD project, a 2D CAD program
**
** Copyright (C) 2010 R. van Twisk (librecad@rvt.dds.nl)
** Copyright (C) 2001-2003 RibbonSoft. All rights reserved.
**
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License version 2 as published by the Free Software 
** Foundation and appearing in the file gpl-2.0.txt included in the
** packaging of this file.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
**
** This copyright notice MUST APPEAR in all copies of the script!  
**
**********************************************************************/

#include "rs_selection.h"

#include "rs_line.h"
#include "rs_information.h"
#include "rs_polyline.h"
#include "rs_ellipse.h"
#include "rs_entity.h"
#include "rs_graphic.h"
#include "rs_layer.h"
#include "rs_modification.h"
#include "lc_searchgraph.h"



/**
 * Default constructor.
 *
 * @param container The container to which we will add
 *        entities. Usually that's an RS_Graphic entity but
 *        it can also be a polyline, text, ...
 */
RS_Selection::RS_Selection(RS_EntityContainer& container,
                           RS_GraphicView* graphicView) {
    this->container = &container;
    this->graphicView = graphicView;
    graphic = container.getGraphic();
}



/**
 * Selects or deselects the given entity.
 */
void RS_Selection::selectSingle(RS_Entity* e) {
	if (e && (! (e->getLayer() && e->getLayer()->isLocked()))) {
        if (graphicView) {
            graphicView->deleteEntity(e);
        }

       	e->toggleSelected();

        if (graphicView) {
            graphicView->drawEntity(e);
        }
    }
}



/**
 * Selects all entities on visible layers.
 */
void RS_Selection::selectAll(bool select) {
    if (graphicView) {
        //graphicView->deleteEntity(container);
    }

	//container->setSelected(select);
	for(auto e: *container){
    //for (unsigned i=0; i<container->count(); ++i) {
        //RS_Entity* e = container->entityAt(i);

        if (e && e->isVisible()) {
            e->setSelected(select);
        }
    }

	if (graphicView) {
        //graphicView->drawEntity(container);
		graphicView->redraw();
    }
}



/**
 * Selects all entities on visible layers.
 */
void RS_Selection::invertSelection() {
    if (graphicView) {
        //graphicView->deleteEntity(container);
    }

	for(auto e: *container){
    //for (unsigned i=0; i<container->count(); ++i) {
        //RS_Entity* e = container->entityAt(i);

        if (e && e->isVisible()) {
            e->toggleSelected();
        }
    }

    if (graphicView) {
        //graphicView->drawEntity(container);
		graphicView->redraw();
    }
}



/**
 * Selects all entities that are completely in the given window.
 *
 * @param v1 First corner of the window to select.
 * @param v2 Second corner of the window to select.
 * @param select true: select, false: deselect
 */
void RS_Selection::selectWindow(const RS_Vector& v1, const RS_Vector& v2,
                                bool select, bool cross) {

    container->selectWindow(v1, v2, select, cross);

    if (graphicView) {
		graphicView->redraw();
    }
}



/**
 * Selects all entities that are intersected by the given line.
 *
 * @param v1 Startpoint of line.
 * @param v2 Endpoint of line.
 * @param select true: select, false: deselect
 */
void RS_Selection::selectIntersected(const RS_Vector& v1, const RS_Vector& v2,
                                     bool select) {

	RS_Line line{v1, v2};
    bool inters;

	for(auto e: *container){
    //for (unsigned i=0; i<container->count(); ++i) {
        //RS_Entity* e = container->entityAt(i);

        if (e && e->isVisible()) {

            inters = false;

            // select containers / groups:
            if (e->isContainer()) {
                RS_EntityContainer* ec = (RS_EntityContainer*)e;

                for (RS_Entity* e2=ec->firstEntity(RS2::ResolveAll); e2;
                        e2=ec->nextEntity(RS2::ResolveAll)) {

                    RS_VectorSolutions sol =
                        RS_Information::getIntersection(&line, e2, true);

                    if (sol.hasValid()) {
                        inters = true;
                    }
                }
            } else {

                RS_VectorSolutions sol =
                    RS_Information::getIntersection(&line, e, true);

                if (sol.hasValid()) {
                    inters = true;
                }
            }

            if (inters) {
                if (graphicView) {
                    graphicView->deleteEntity(e);
                }

                e->setSelected(select);

                if (graphicView) {
                    graphicView->drawEntity(e);
                }
            }
        }
    }

}



/**
 * Selects all entities that are connected to the given entity.
 *
 * @param e The entity where the algorithm starts. Must be an atomic entity.
 */
void RS_Selection::selectContour(RS_Entity* e, QList<RS_Entity*>* preview) {

	if (!e || !e->isAtomic())
		return;

	QList<RS_Entity*> list, list2;
	bool select = !e->isSelected();
	if (!findContour(e, RS2::EndingStart, list)) {
		findContour(e, RS2::EndingEnd, list2);
		for (auto e2 : list2)
			if (!list.contains(e2))
				list.append(e2);
	}
	if (preview == nullptr)
		setSelected(list, select);
	else for (auto e2 : list)
		preview->append(e2->clone());
}

/**
 * Select a "contour" of intersecting entities, between the specified entities.
 * If the contour can be closed, and there are 2 valid paths, the shortest path is selected.  
 * If the paths are equal length, the path closest to the cursor is selected.
 *
 * @param preview if specified; instead of selecting the path, return a cloned copy of it 
 */
void RS_Selection::selectContour(RS_Entity * start, RS_Entity * end, const RS_Vector& cursor, QList<RS_Entity*>* preview)
{
	if (!end)
		return;
	if (!start) {
		if (preview != nullptr)
			preview->append(end->clone());
		else
			selectSingle(end);
		return;
	}

	QList<RS_Entity*> fromStart, fromEnd, list;
	double startLength(0), endLength(0);
	bool select = !end->isSelected();
	bool foundShortestPath = false;

	if (list.size() == 0) {
		findContour(start, RS2::EndingStart, fromStart);
		findContour(start, RS2::EndingEnd, fromEnd);

		while (!fromStart.empty() && fromStart.last() != end)
			fromStart.removeLast();
		while (!fromEnd.empty() && fromEnd.last() != end)
			fromEnd.removeLast();

		for (auto e : fromStart)
			startLength += e->getLength();
		for (auto e : fromEnd)
			endLength += e->getLength();

		if (fabs(endLength - startLength) < RS_TOLERANCE && startLength > RS_TOLERANCE)
		{
			if (start->getStartpoint().distanceTo(cursor) < start->getEndpoint().distanceTo(cursor))
				list = fromStart;
			else
				list = fromEnd;
		}
		else if (startLength > RS_TOLERANCE && startLength < endLength)
			list = fromStart;
		else if (endLength > RS_TOLERANCE && endLength < startLength)
			list = fromEnd;
		else { // startLength and endLength were both zero; no path
			if (start->isAtomic() && end->isAtomic()) { // try A-star
				LC_SelectionSearch graph(container, graphicView);
				foundShortestPath = graph.selectShortestPath(static_cast<RS_AtomicEntity*>(start), static_cast<RS_AtomicEntity*>(end), cursor, preview);
			}
		}
			
	}

	if (preview == nullptr)
		setSelected(list, select);
	else if (!foundShortestPath) {
		for (auto e : list)
			preview->append(e->clone());
	}
}

/**
 * Find a "contour" of intersecting entities, from the specified end of the (atomic) entity.
 * In the case of a branching path, the algorithm simply follows the first one it encounters.
 * Closed-circular entities are not considered a valid portion of a contour (unless they are the start entity)
 *
 * @return true if the contour was closed and connects back to the start entity
 */
bool RS_Selection::findContour(RS_Entity * start, RS2::Ending end, QList<RS_Entity*>& list)
{
	if (start == NULL) 
		return false;
	if (!start->isAtomic() && start->rtti() != RS2::EntitySpline)
		return false;

	double INTERSECT_TOL = start->rtti() == RS2::EntitySpline ? 1.0e-3 : RS_TOLERANCE_TRIM;
	RS_Vector startPoint(start->getStartpoint()), endPoint(start->getEndpoint());
	if (end != RS2::EndingStart)
		std::swap(startPoint, endPoint);

	list.append(start);

	if (start->isClosedContour())
		return true;

	RS_Vector p1 = startPoint;
	bool found, add;

	do
	{
		found = false;

		for (auto en : *container) {
			add = false;

			if (en && en != start && en->isVisible() && !en->isConstruction() && 
				(!(en->getLayer() && en->getLayer()->isLocked()))) {

				if (en->isClosedContour() || list.contains(en))
					continue;

				// startpoint connects to 1st point
				if (en->getStartpoint().distanceTo(p1) < INTERSECT_TOL) {
					p1 = en->getEndpoint();
					add = true;
				}

				// endpoint connects to 1st point
				else if (en->getEndpoint().distanceTo(p1) < INTERSECT_TOL) {
					p1 = en->getStartpoint();
					add = true;
				}

				if (add) {
					list.append(en);
					found = true;
				}

				if (p1.distanceTo(endPoint) < INTERSECT_TOL) { // we've connected back to the start point
					return true;
				}
			}
		}
	} while (found);
	return false;
}

void RS_Selection::setSelected(QList<RS_Entity*>& list, bool select)
{
	for (auto e : list) {
		if (graphicView) {
			graphicView->deleteEntity(e);
		}
		
		e->setSelected(select);

		if (graphicView) {
			graphicView->drawEntity(e);
		}
	}
}


/**
 * Selects all entities on the given layer.
 */
void RS_Selection::selectLayer(RS_Entity* e) {

    if (e==NULL) {
        return;
    }

    bool select = !e->isSelected();

    RS_Layer* layer = e->getLayer(true);
    if (layer==NULL) {
        return;
    }

    QString layerName = layer->getName();
	selectLayer(layerName, select);
}



/**
 * Selects all entities on the given layer.
 */
void RS_Selection::selectLayer(const QString& layerName, bool select) {

	for(auto en: *container){

        if (en && en->isVisible() && 
				en->isSelected()!=select && 
				(!(en->getLayer() && en->getLayer()->isLocked()))) {

            RS_Layer* l = en->getLayer(true);

            if (l && l->getName()==layerName) {
                if (graphicView) {
                    graphicView->deleteEntity(en);
                }
                en->setSelected(select);
                if (graphicView) {
                    graphicView->drawEntity(en);
                }
            }
        }
    }
}

// EOF
