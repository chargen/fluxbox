// Container.cc
// Copyright (c) 2003 - 2006 Henrik Kinnunen (fluxgen at fluxbox dot org)
//                and Simon Bowden    (rathnor at users.sourceforge.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Container.hh"

#include "Button.hh"
#include "TextUtils.hh"
#include "EventManager.hh"
#include "CompareEqual.hh"

#include <algorithm>

namespace FbTk {

typedef CompareEqual_base<FbWindow, Window> CompareWindow;

Container::Container(const FbWindow &parent, bool auto_resize):
    FbWindow(parent, 0, 0, 1, 1, ExposureMask),
    m_orientation(ROT0),
    m_align(RELATIVE),
    m_max_size_per_client(60),
    m_max_total_size(0),
    m_update_lock(false),
    m_auto_resize(auto_resize) {
    EventManager::instance()->add(*this, *this);
}

Container::~Container() {
    // ~FbWindow cleans event manager
}

void Container::resize(unsigned int width, unsigned int height) {
    // do we need to resize?
    if (FbWindow::width() == width &&
        FbWindow::height() == height)
        return;

    FbWindow::resize(width, height);
    repositionItems();
}

void Container::moveResize(int x, int y,
                           unsigned int width, unsigned int height) {
    FbWindow::moveResize(x, y, width, height);
    repositionItems();
}

void Container::insertItem(Item item, int pos) {
    if (find(item) != -1)
        return;

    // it must be a child of this window
    if (item->parent() != this)
        return;

    item->setOrientation(m_orientation);
    if (pos >= size() || pos < 0) {
        m_item_list.push_back(item);
    } else if (pos == 0) {
        m_item_list.push_front(item);
    } else {
        ItemList::iterator it = m_item_list.begin();
        for (; pos != 0; ++it, --pos)
            continue;

        m_item_list.insert(it, item);
    }

    // make sure we dont have duplicate items
    m_item_list.unique();

    repositionItems();
}

void Container::moveItem(Item item, int movement) {

    int index = find(item);
    const size_t size = m_item_list.size();

    if (index < 0 || (movement % static_cast<signed>(size)) == 0) {
        return;
    }

    int newindex = (index + movement) % static_cast<signed>(size);
    if (newindex < 0) // neg wrap
        newindex += size;

    ItemList::iterator it = std::find(m_item_list.begin(),
                                      m_item_list.end(),
                                      item);
    m_item_list.erase(it);

    for (it = m_item_list.begin(); newindex >= 0; ++it, --newindex) {
        if (newindex == 0) {
            break;
        }
    }

    m_item_list.insert(it, item);
    repositionItems();
}

// returns true if something was done
bool Container::moveItemTo(Item item, int x, int y) {
    Window parent_return=0,
        root_return=0,
        *children_return = NULL;

    unsigned int nchildren_return;

    // get the root window
    if (!XQueryTree(display(), window(),
                    &root_return, &parent_return, &children_return, &nchildren_return))
        parent_return = parent_return;

    if (children_return != NULL)
        XFree(children_return);

    int dest_x = 0, dest_y = 0;
    Window itemwin = 0;
    if (!XTranslateCoordinates(display(),
                               root_return, window(),
                               x, y, &dest_x, &dest_y,
                               &itemwin))
        return false;

    ItemList::iterator it = find_if(m_item_list.begin(),
                                    m_item_list.end(),
                                    CompareWindow(&FbWindow::window,
                                                  itemwin));
    // not found :(
    if (it == m_item_list.end())
        return false;

    Window child_return = 0;
    //make x and y relative to our item
    if (!XTranslateCoordinates(display(),
                               window(), itemwin,
                               dest_x, dest_y, &x, &y,
                               &child_return))
        return false;
    return true;
}

bool Container::removeItem(Item item) {
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end && (*it) != item; ++it);

    if (it == it_end)
        return false;

    m_item_list.erase(it);
    repositionItems();
    return true;
}

bool Container::removeItem(int index) {
    if (index < 0 || index > size())
        return false;

    ItemList::iterator it = m_item_list.begin();
    for (; index != 0; ++it, --index)
        continue;

    m_item_list.erase(it);

    repositionItems();
    return true;
}

void Container::removeAll() {
    m_item_list.clear();
    if (!m_update_lock) {
        clear();
    }

}

int Container::find(ConstItem item) {
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    int index = 0;
    for (; it != it_end; ++it, ++index) {
        if ((*it) == item)
            break;
    }

    if (it == it_end)
        return -1;

    return index;
}

void Container::setMaxSizePerClient(unsigned int size) {
    if (size != m_max_size_per_client) {
        m_max_size_per_client = size;
        repositionItems();
    }
}

void Container::setMaxTotalSize(unsigned int size) {
    if (m_max_total_size == size)
        return;

    m_max_total_size = size;

    repositionItems();
    return;
}

void Container::setAlignment(Container::Alignment a) {
    if (m_align != a) {
        m_align = a;
        repositionItems();
    }
}

void Container::exposeEvent(XExposeEvent &event) {
    if (!m_update_lock) {
        clearArea(event.x, event.y, event.width, event.height);
    }
}

bool Container::tryExposeEvent(XExposeEvent &event) {
    if (event.window == window()) {
        exposeEvent(event);
        return true;
    }

    ItemList::iterator it = find_if(m_item_list.begin(),
                                    m_item_list.end(),
                                    CompareWindow(&FbWindow::window,
                                                  event.window));
    // not found :(
    if (it == m_item_list.end())
        return false;

    (*it)->exposeEvent(event);
    return true;
}

bool Container::tryButtonPressEvent(XButtonEvent &event) {
    if (event.window == window()) {
        // we don't have a buttonrelease event atm
        return true;
    }

    ItemList::iterator it = find_if(m_item_list.begin(),
                                    m_item_list.end(),
                                    CompareWindow(&FbWindow::window,
                                                  event.window));
    // not found :(
    if (it == m_item_list.end())
        return false;

    (*it)->buttonPressEvent(event);
    return true;
}

bool Container::tryButtonReleaseEvent(XButtonEvent &event) {
    if (event.window == window()) {
        // we don't have a buttonrelease event atm
        return true;
    }

    ItemList::iterator it = find_if(m_item_list.begin(),
                                    m_item_list.end(),
                                    CompareWindow(&FbWindow::window,
                                                  event.window));
    // not found :(
    if (it == m_item_list.end())
        return false;

    (*it)->buttonReleaseEvent(event);
    return true;
}

void Container::repositionItems() {
    if (empty() || m_update_lock)
        return;

    /**
       NOTE: all calculations here are done in non-rotated space
     */

    unsigned int max_width_per_client = maxWidthPerClient();
    unsigned int borderW = m_item_list.front()->borderWidth();
    size_t num_items = m_item_list.size();

    unsigned int total_width;
    unsigned int cur_width;
    unsigned int height;

    // unrotate
    if (m_orientation == ROT0 || m_orientation == ROT180) {
        total_width = cur_width = width();
        height = this->height();
    } else {
        total_width = cur_width = this->height();
        height = width();
    }

    // if we have a max total size, then we must also resize ourself
    // within that bound
    Alignment align = alignment();
    if (m_max_total_size && align != RELATIVE) {
        total_width = (max_width_per_client + borderW) * num_items - borderW;
        if (total_width > m_max_total_size) {
            total_width = m_max_total_size;
            if (m_max_total_size > ((num_items - 1)*borderW)) { // don't go negative with unsigned nums
                max_width_per_client = ( m_max_total_size - (num_items - 1)*borderW ) / num_items;
            } else
                max_width_per_client = 1;
        }
        if (m_auto_resize && total_width != cur_width) {
            // calling Container::resize here risks infinite loops
            unsigned int neww = total_width, newh = height;
            translateSize(m_orientation, neww, newh);
            if (!(align == LEFT && (m_orientation == ROT0 ||
                                     m_orientation == ROT90)) &&
                !(align == RIGHT && (m_orientation == ROT180 ||
                                     m_orientation == ROT270))) {
                int deltax = 0;
                int deltay = 0;
                if (m_orientation == ROT0 || m_orientation == ROT180)
                    deltax = - (total_width - cur_width);
                else
                    deltay = - (total_width - cur_width);
                // TODO: rounding errors could accumulate in this process
                if (align == CENTER) {
                    deltax = deltax/2;
                    deltay = deltay/2;
                }

                FbWindow::moveResize(x() + deltax, y() + deltay, neww, newh);
            } else {
                FbWindow::resize(neww, newh);
            }
        }
    }


    ItemList::iterator it = m_item_list.begin();
    const ItemList::iterator it_end = m_item_list.end();

    int rounding_error = 0;

    if (align == RELATIVE || total_width == m_max_total_size) {
        rounding_error = total_width - ((max_width_per_client + borderW)* num_items - borderW);
    }

    int next_x = -borderW; // zero so the border of the first shows
    int extra = 0;
    int direction = 1;
    if (align == RIGHT) {
        direction = -1;
        next_x = total_width - max_width_per_client - borderW;
    }

    int tmpx, tmpy;
    unsigned int tmpw, tmph;
    for (; it != it_end; ++it, next_x += direction*(max_width_per_client + borderW + extra)) {
        // we only need to do error stuff with alignment RELATIVE
        // OR with max_total_size triggered
        if (rounding_error) {
            --rounding_error;
            extra = 1;
            //counter for different direction
            if (align == RIGHT && !extra)
                --next_x;
        } else {
            if (extra && align == RIGHT) // last extra
                ++next_x;
            extra = 0;
        }
        // rotate the x and y coords
        tmpx = next_x;
        tmpy = -borderW;
        tmpw = max_width_per_client + extra;
        tmph = height;

        translateCoords(m_orientation, tmpx, tmpy, total_width, height);
        translatePosition(m_orientation, tmpx, tmpy, tmpw, tmph, borderW);
        translateSize(m_orientation, tmpw, tmph);

        // resize each clients including border in size
        (*it)->moveResize(tmpx, tmpy,
                          tmpw, tmph);

        // moveresize does a clear
    }

}


unsigned int Container::maxWidthPerClient() const {
    switch (alignment()) {
    case RIGHT:
    case CENTER:
    case LEFT:
        return m_max_size_per_client;
        break;
    case RELATIVE:
        if (size() == 0)
            return width();
        else {
            unsigned int borderW = m_item_list.front()->borderWidth();
            // there're count-1 borders to fit in with the windows
            // -> 1 per window plus end
            unsigned int w = width(), h = height();
            translateSize(m_orientation, w, h);
            if (w < (size()-1)*borderW)
                return 1;
            else
                return (w - (size() - 1) * borderW) / size();
        }
        break;
    }

    // this will never happen anyway
    return 1;
}

void Container::for_each(std::mem_fun_t<void, FbWindow> function) {
    std::for_each(m_item_list.begin(),
                  m_item_list.end(),
                  function);
}

void Container::setAlpha(unsigned char alpha) {
    FbWindow::setAlpha(alpha);
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end; ++it)
        (*it)->setAlpha(alpha);
}

void Container::parentMoved() {
    FbWindow::parentMoved();
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end; ++it)
        (*it)->parentMoved();
}

void Container::invalidateBackground() {
    FbWindow::invalidateBackground();
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end; ++it)
        (*it)->invalidateBackground();
}

void Container::clear() {
    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end; ++it)
        (*it)->clear();

}

void Container::setOrientation(Orientation orient) {
    if (m_orientation == orient)
        return;

    FbWindow::invalidateBackground();

    ItemList::iterator it = m_item_list.begin();
    ItemList::iterator it_end = m_item_list.end();
    for (; it != it_end; ++it)
        (*it)->setOrientation(orient);

    if (((m_orientation == ROT0 || m_orientation == ROT180) &&
        (orient == ROT90 || orient == ROT270)) ||
        ((m_orientation == ROT90 || m_orientation == ROT270) &&
        (orient == ROT0 || orient == ROT180))) {
        // flip width and height
        m_orientation = orient;
        resize(height(), width());
    } else {
        m_orientation = orient;
        repositionItems();
    }

}

} // end namespace FbTk
