/* Copyright (C) 2004-2008 Robert Griebl. All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU
** General Public License version 2 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <QPainter>
#include <QStyle>
#include <QList>
#include <QMenu>
#include <QCursor>
#include <QAction>
#include <QToolTip>
#include <QEvent>
#include <QPaintEvent>
#include <QStyleOptionHeader>
#include <QDesktopServices>

#include "bricklink.h"
#include "config.h"
#include "currency.h"

#include "priceguidewidget.h"

namespace {

static const int hborder = 4;
static const int vborder = 4;

struct cell : public QRect {
    enum cell_type {
        Header,
        Quantity,
        Price,
        Update,
        Empty
    };

    cell() : QRect()
    { }

    cell(cell_type t, int x, int y, int w, int h, int tfl, const QString &str, bool flag = false)
            : QRect(x, y, w, h), m_type(t), m_text_flags(tfl), m_text(str), m_flag(flag)
    { }

    cell_type     m_type;
    Qt::Alignment m_text_flags;
    QString       m_text;
    bool          m_flag;

    BrickLink::Price m_price;
    BrickLink::Time  m_time;
    BrickLink::Condition         m_condition;
};

} // namespace

class PriceGuideWidgetPrivate {
public:
    PriceGuideWidgetPrivate(PriceGuideWidget *parent)
            : m_widget(parent)
    { }

    virtual ~PriceGuideWidgetPrivate()
    { }

    QList<cell>::const_iterator cellAtPos(const QPoint &fpos)
    {
        QPoint pos = fpos - QPoint(m_widget->frameWidth(), m_widget->frameWidth());
        QList<cell>::const_iterator it = m_cells.begin();

        for (; it != m_cells.end(); ++it) {
            if ((*it).contains(pos) && ((*it).m_type != cell::Update))
                break;
        }
        return it;
    }

protected:
private:
    friend class PriceGuideWidget;

    PriceGuideWidget *        m_widget;
    BrickLink::PriceGuide *   m_pg;
    PriceGuideWidget::Layout  m_layout;
    QList<cell>               m_cells;
    bool                      m_connected;
    bool                      m_on_price;

    QString m_str_qty;
    QString m_str_cond [BrickLink::ConditionCount];
    QString m_str_price [BrickLink::PriceCount];
    QString m_str_vtime [BrickLink::TimeCount];
    QString m_str_htime [BrickLink::TimeCount][2];
    QString m_str_wait;
};

PriceGuideWidget::PriceGuideWidget(QWidget *parent, Qt::WindowFlags f)
        : QFrame(parent, f)
{
    d = new PriceGuideWidgetPrivate(this);

    d->m_pg = 0;
    d->m_layout = Normal;
    d->m_on_price = false;

    setBackgroundRole(QPalette::Base);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    setContextMenuPolicy(Qt::ActionsContextMenu);

    d->m_connected = false;

    connect(Config::inst(), SIGNAL(localCurrencyChanged()), this, SLOT(update()));

    QAction *a;
    a = new QAction(this);
    a->setObjectName("priceguide_reload");
    a->setIcon(QIcon(":/images/reload"));
    connect(a, SIGNAL(triggered()), this, SLOT(doUpdate()));
    addAction(a);

    a = new QAction(this);
    a->setSeparator(true);
    addAction(a);

    a = new QAction(this);
    a->setObjectName("priceguide_bl_catalog");
    a->setIcon(QIcon(":/images/edit_bl_catalog"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLCatalogInfo()));
    addAction(a);
    a = new QAction(this);
    a->setObjectName("priceguide_bl_priceguide");
    a->setIcon(QIcon(":/images/edit_bl_priceguide"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLPriceGuideInfo()));
    addAction(a);
    a = new QAction(this);
    a->setObjectName("priceguide_bl_lotsforsale");
    a->setIcon(QIcon(":/images/edit_bl_lotsforsale"));
    connect(a, SIGNAL(triggered()), this, SLOT(showBLLotsForSale()));
    addAction(a);


    languageChange();
}

void PriceGuideWidget::languageChange()
{
    findChild<QAction *>("priceguide_reload")->setText(tr("Update"));
    findChild<QAction *>("priceguide_bl_catalog")->setText(tr("Show BrickLink Catalog Info..."));
    findChild<QAction *>("priceguide_bl_priceguide")->setText(tr("Show BrickLink Price Guide Info..."));
    findChild<QAction *>("priceguide_bl_lotsforsale")->setText(tr("Show Lots for Sale on BrickLink..."));

    d->m_str_qty                           = tr("Qty.");
    d->m_str_cond [BrickLink::New]         = tr("New");
    d->m_str_cond [BrickLink::Used]        = tr("Used");
    d->m_str_price [BrickLink::Lowest]     = tr("Min.");
    d->m_str_price [BrickLink::Average]    = tr("Avg.");
    d->m_str_price [BrickLink::WAverage]   = tr("Q.Avg.");
    d->m_str_price [BrickLink::Highest]    = tr("Max.");
    d->m_str_htime [BrickLink::PastSix][0] = tr("Last 6");
    d->m_str_htime [BrickLink::PastSix][1] = tr("Months Sales");
    d->m_str_htime [BrickLink::Current][0] = tr("Current");
    d->m_str_htime [BrickLink::Current][1] = tr("Inventory");
    d->m_str_vtime [BrickLink::PastSix]    = tr("Last 6 Months Sales");
    d->m_str_vtime [BrickLink::Current]    = tr("Current Inventory");
    d->m_str_wait                          = tr("Please wait ...updating");

    recalcLayout();
    update();
}

PriceGuideWidget::~PriceGuideWidget()
{
    if (d->m_pg)
        d->m_pg->release();

    delete d;
}

void PriceGuideWidget::showBLCatalogInfo()
{
    if (d->m_pg && d->m_pg->item())
        QDesktopServices::openUrl(BrickLink::core()->url(BrickLink::URL_CatalogInfo, d->m_pg->item()));
}

void PriceGuideWidget::showBLPriceGuideInfo()
{
    if (d->m_pg && d->m_pg->item() && d->m_pg->color())
        QDesktopServices::openUrl(BrickLink::core()->url(BrickLink::URL_PriceGuideInfo, d->m_pg->item(), d->m_pg->color()));
}

void PriceGuideWidget::showBLLotsForSale()
{
    if (d->m_pg && d->m_pg->item() && d->m_pg->color())
        QDesktopServices::openUrl(BrickLink::core()->url(BrickLink::URL_LotsForSale, d->m_pg->item(), d->m_pg->color()));
}

QSize PriceGuideWidget::sizeHint() const
{
    return minimumSize();
}

void PriceGuideWidget::doUpdate()
{
    if (d->m_pg)
        d->m_pg->update(true);
    update(nonStaticCells());
}


void PriceGuideWidget::gotUpdate(BrickLink::PriceGuide *pg)
{
    if (pg == d->m_pg)
        update(nonStaticCells());
}


void PriceGuideWidget::setPriceGuide(BrickLink::PriceGuide *pg)
{
    if (pg == d->m_pg)
        return;

    if (d->m_pg)
        d->m_pg->release();
    if (pg)
        pg->addRef();

    if (!d->m_connected && pg)
        d->m_connected = connect(BrickLink::core(), SIGNAL(priceGuideUpdated(BrickLink::PriceGuide *)), this, SLOT(gotUpdate(BrickLink::PriceGuide *)));

    d->m_pg = pg;
    update(nonStaticCells());
}

BrickLink::PriceGuide *PriceGuideWidget::priceGuide() const
{
    return d->m_pg;
}

void PriceGuideWidget::setLayout(Layout l)
{
    if (l != d->m_layout) {
        d->m_layout = l;
        recalcLayout();
        update();
    }
}

void PriceGuideWidget::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    recalcLayout();
}

void PriceGuideWidget::recalcLayout()
{
    d->m_cells.clear();

    ensurePolished();
    const QFontMetrics &fm = fontMetrics();

    QFont fb = font();
    fb.setBold(true);
    QFontMetrics fmb(fb);

    QSize s = contentsRect().size();

    switch (d->m_layout) {
    case Normal    : recalcLayoutNormal(s, fm, fmb); break;
    case Horizontal: recalcLayoutHorizontal(s, fm, fmb); break;
    case Vertical  : recalcLayoutVertical(s, fm, fmb); break;
    }
}

void PriceGuideWidget::recalcLayoutNormal(const QSize &s, const QFontMetrics &fm, const QFontMetrics &fmb)
{
    int cw [4];
    int ch = fm.height() + 2 * vborder;

    int dx, dy;

    cw [0] = 0; // not used
    cw [1] = 0;
    for (int i = 0; i < BrickLink::ConditionCount; i++)
        cw [1] = qMax(cw [1], fm.width(d->m_str_cond [i]));
    cw [2] = qMax(fm.width(d->m_str_qty), fm.width("0000 (000000)"));
    cw [3] = fm.width(Currency(9000).toLocal(Currency::LocalSymbol));
    for (int i = 0; i < BrickLink::PriceCount; i++)
        cw [3] = qMax(cw [3], fm.width(d->m_str_price [i]));

    for (int i = 1; i < 4; i++)
        cw [i] += (2 * hborder);

    dx = 0;
    for (int i = 0; i < BrickLink::TimeCount; i++)
        dx = qMax(dx, fmb.width(d->m_str_vtime [i]));

    if ((cw [1] + cw [2] + BrickLink::PriceCount * cw [3]) < dx)
        cw [1] = dx - (cw [2] + BrickLink::PriceCount * cw [3]);

    setMinimumSize(2 * frameWidth() + cw [1] + cw [2] + BrickLink::PriceCount * cw [3],
                   2 * frameWidth() + (1 + (1 + BrickLink::ConditionCount) * BrickLink::TimeCount) * ch);

    dx = cw [1];
    dy = 0;

    d->m_cells << cell(cell::Header, 0, 0, cw [1], ch, Qt::AlignCenter, "$$$", true);
    d->m_cells << cell(cell::Header, cw [1], 0, cw [2], ch, Qt::AlignCenter, d->m_str_qty);

    dx = cw [1] + cw [2];

    for (int i = 0; i < BrickLink::PriceCount; i++) {
        d->m_cells << cell(cell::Header, dx, 0, cw [3], ch, Qt::AlignCenter, d->m_str_price [i]);
        dx += cw [3];
    }

    d->m_cells << cell(cell::Header, dx, 0, s.width() - dx, ch, 0, QString::null);

    dx = cw [1] + cw [2] + cw [3] * BrickLink::PriceCount;
    dy = ch;

    for (int i = 0; i < BrickLink::TimeCount; i++) {
        d->m_cells << cell(cell::Header, 0, dy, dx, ch, Qt::AlignCenter, d->m_str_vtime [i], true);
        dy += ch;

        for (int j = 0; j < BrickLink::ConditionCount; j++)
            d->m_cells << cell(cell::Header, 0, dy + j * ch, cw [1], ch, Qt::AlignCenter, d->m_str_cond [j]);

        d->m_cells << cell(cell::Update, cw [1], dy, dx - cw [1], ch * BrickLink::ConditionCount, Qt::AlignCenter | Qt::TextWordWrap, QString::null);
        dy += (BrickLink::ConditionCount * ch);
    }

    d->m_cells << cell(cell::Header, 0, dy, cw [1], s.height() - dy, 0, QString::null);

    dy = ch;
    bool flip = false;

    for (BrickLink::Time i = BrickLink::Time(0); i < BrickLink::TimeCount; i = BrickLink::Time(i+1)) {
        dy += ch;

        for (BrickLink::Condition j = BrickLink::Condition(0); j < BrickLink::ConditionCount; j = BrickLink::Condition(j+1)) {
            dx = cw [1];

            cell c(cell::Quantity, dx, dy, cw [2], ch, Qt::AlignRight | Qt::AlignVCenter, QString::null, flip);
            c.m_time      = i;
            c.m_condition = j;
            d->m_cells << c;

            dx += cw [2];

            for (BrickLink::Price k = BrickLink::Price(0); k < BrickLink::PriceCount; k = BrickLink::Price(k+1)) {
                cell c(cell::Price, dx, dy, cw [3], ch, Qt::AlignRight  | Qt::AlignVCenter, QString::null, flip);
                c.m_time      = i;
                c.m_condition = j;
                c.m_price     = k;
                d->m_cells << c;

                dx += cw [3];
            }
            dy += ch;
            flip = !flip;
        }
    }
}


void PriceGuideWidget::recalcLayoutHorizontal(const QSize &s, const QFontMetrics &fm, const QFontMetrics &fmb)
{
    int cw [4];
    int ch = fm.height() + 2 * vborder;

    int dx, dy;

    cw [0] = 0;
    for (int i = 0; i < BrickLink::TimeCount; i++)
        cw [0] = qMax(cw [0], qMax(fmb.width(d->m_str_htime [i][0]), fmb.width(d->m_str_htime [i][1])));
    cw [1] = 0;
    for (int i = 0; i < BrickLink::ConditionCount; i++)
        cw [1] = qMax(cw [1], fm.width(d->m_str_cond [i]));
    cw [2] = qMax(fm.width(d->m_str_qty), fm.width("0000 (000000)"));
    cw [3] = fm.width(Currency (9000).toLocal(Currency::NoSymbol));
    for (int i = 0; i < BrickLink::PriceCount; i++)
        cw [3] = qMax(cw [3], fm.width(d->m_str_price [i]));

    for (int i = 0; i < 4; i++)
        cw [i] += (2 * hborder);

    setMinimumSize(2 * frameWidth() + cw [0] + cw [1] + cw [2] + BrickLink::PriceCount * cw [3],
                   2 * frameWidth() + (1 + BrickLink::ConditionCount * BrickLink::TimeCount) * ch);

    dx = cw [0] + cw [1];
    dy = 0;

    d->m_cells << cell(cell::Header, 0,  dy, dx,     ch, Qt::AlignCenter, "$$$", true);
    d->m_cells << cell(cell::Header, dx, dy, cw [2], ch, Qt::AlignCenter, d->m_str_qty);

    dx += cw [2];

    for (int i = 0; i < BrickLink::PriceCount; i++) {
        d->m_cells << cell(cell::Header, dx, dy, cw [3], ch, Qt::AlignCenter, d->m_str_price [i]);
        dx += cw [3];
    }

    d->m_cells << cell(cell::Header, dx, dy, s.width() - dx, ch, 0, QString::null);

    dx = 0;
    dy = ch;

    for (int i = 0; i < BrickLink::TimeCount; i++) {
        d->m_cells << cell(cell::Header, dx, dy, cw [0], BrickLink::ConditionCount * ch, Qt::AlignLeft | Qt::AlignVCenter, d->m_str_htime [i][0] + "\n" + d->m_str_htime [i][1], true);

        for (int j = 0; j < BrickLink::ConditionCount; j++) {
            d->m_cells << cell(cell::Header, dx + cw [0], dy, cw [1], ch, Qt::AlignCenter, d->m_str_cond [j]);
            dy += ch;
        }
    }

    d->m_cells << cell(cell::Header, dx, dy, cw [0] + cw [1], s.height() - dy, 0, QString::null);

    d->m_cells << cell(cell::Update, cw [0] + cw [1], ch, cw [2] + BrickLink::PriceCount * cw [3], BrickLink::TimeCount * BrickLink::ConditionCount * ch, Qt::AlignCenter | Qt::TextWordWrap, QString::null);

    dy = ch;
    bool flip = false;

    for (BrickLink::Time i = BrickLink::Time(0); i < BrickLink::TimeCount; i = BrickLink::Time(i+1)) {
        for (BrickLink::Condition j = BrickLink::Condition(0); j < BrickLink::ConditionCount; j = BrickLink::Condition(j+1)) {
            dx = cw [0] + cw [1];

            cell c(cell::Quantity, dx, dy, cw [2], ch, Qt::AlignRight | Qt::AlignVCenter, QString::null, flip);
            c.m_time      = i;
            c.m_condition = j;
            d->m_cells << c;
            dx += cw [2];

            for (BrickLink::Price k = BrickLink::Price(0); k < BrickLink::PriceCount; k = BrickLink::Price(k+1)) {
                cell c(cell::Price, dx, dy, cw [3], ch, Qt::AlignRight  | Qt::AlignVCenter, QString::null, flip);
                c.m_time      = i;
                c.m_condition = j;
                c.m_price     = k;
                d->m_cells << c;
                dx += cw [3];
            }
            dy += ch;
            flip = !flip;
        }
    }
}


void PriceGuideWidget::recalcLayoutVertical(const QSize &s, const QFontMetrics &fm, const QFontMetrics &fmb)
{
    int cw [2];
    int ch = fm.height() + 2 * vborder;

    int dx, dy;

    cw [0] = fm.width(d->m_str_qty);

    for (int i = 0; i < BrickLink::PriceCount; i++)
        cw [0] = qMax(cw [0], fm.width(d->m_str_price [i]));
    cw [0] += 2 * hborder;

    cw [1] = qMax(fm.width(Currency (9000).toLocal(Currency::NoSymbol)), fm.width("0000 (000000)"));
    for (int i = 0; i < BrickLink::ConditionCount; i++)
        cw [1] = qMax(cw [1], fm.width(d->m_str_cond [i]));
    cw [1] += 2 * hborder;

    dx = 0;
    for (int i = 0; i < BrickLink::TimeCount; i++)
        dx = qMax(dx, fmb.width(d->m_str_vtime [i]));

    if (dx > (cw [0] + BrickLink::ConditionCount * cw [1])) {
        dx -= (cw [0] + BrickLink::ConditionCount * cw [1]);
        dx = (dx + BrickLink::ConditionCount) / (BrickLink::ConditionCount + 1);

        cw [0] += dx;
        cw [1] += dx;
    }
    setMinimumSize(2 * frameWidth() + cw [0] + BrickLink::ConditionCount * cw [1],
                   2 * frameWidth() + (1 + BrickLink::TimeCount * (2 + BrickLink::PriceCount)) * ch);

    dx = 0;
    dy = 0;

    d->m_cells << cell(cell::Header, dx, dy, cw [0], ch, Qt::AlignCenter, "$$$", true);
    dx += cw [0];

    for (int i = 0; i < BrickLink::ConditionCount; i++) {
        d->m_cells << cell(cell::Header, dx, dy, cw [1], ch, Qt::AlignCenter, d->m_str_cond [i]);
        dx += cw [1];
    }

    d->m_cells << cell(cell::Header, dx, dy, s.width() - dx, ch, 0, QString::null);
    dy += ch;

    d->m_cells << cell(cell::Empty, dx, dy, s.width() - dx, s.height() - dy, 0, QString::null);

    dx = 0;
    dy = ch;

    for (int i = 0; i < BrickLink::TimeCount; i++) {
        d->m_cells << cell(cell::Header, dx, dy, cw [0] + BrickLink::ConditionCount * cw [1], ch, Qt::AlignCenter, d->m_str_vtime [i], true);
        dy += ch;

        d->m_cells << cell(cell::Header, dx, dy, cw [0], ch, Qt::AlignLeft | Qt::AlignVCenter, d->m_str_qty, false);

        d->m_cells << cell(cell::Update, dx + cw [0], dy, BrickLink::ConditionCount * cw [1], (1 + BrickLink::PriceCount) * ch, Qt::AlignCenter | Qt::TextWordWrap, QString::null);
        dy += ch;

        for (int j = 0; j < BrickLink::PriceCount; j++) {
            d->m_cells << cell(cell::Header, dx, dy, cw [0], ch, Qt::AlignLeft | Qt::AlignVCenter, d->m_str_price [j], false);
            dy += ch;
        }
    }
    d->m_cells << cell(cell::Header, dx, dy, cw [0], s.height() - dy, 0, QString::null);

    d->m_cells << cell(cell::Empty, dx + cw [0], dy, BrickLink::ConditionCount * cw [1], s.height() - dy, 0, QString::null);


    dx = cw [0];

    for (BrickLink::Condition j = BrickLink::Condition(0); j < BrickLink::ConditionCount; j = BrickLink::Condition(j+1)) {
        dy = ch;

        for (BrickLink::Time i = BrickLink::Time(0); i < BrickLink::TimeCount; i = BrickLink::Time(i+1)) {
            dy += ch;

            cell c(cell::Quantity, dx, dy, cw [1], ch, Qt::AlignRight | Qt::AlignVCenter, QString::null, false);
            c.m_time = i;
            c.m_condition = j;
            d->m_cells << c;
            dy += ch;

            bool flip = true;

            for (BrickLink::Price k = BrickLink::Price(0); k < BrickLink::PriceCount; k = BrickLink::Price(k+1)) {
                cell c(cell::Price, dx, dy, cw [1], ch, Qt::AlignRight | Qt::AlignVCenter, QString::null, flip);
                c.m_time = i;
                c.m_condition = j;
                c.m_price = k;
                d->m_cells << c;
                dy += ch;
                flip = !flip;
            }
        }
        dx += cw [1];
    }
}



void PriceGuideWidget::paintHeader(QPainter *p, const QRect &r, Qt::Alignment align, const QString &str, bool bold)
{
    QStyleOptionHeader opt;
    opt.initFrom(this);
    opt.state           &= ~QStyle::State_MouseOver;
    opt.rect             = r;
    opt.orientation      = Qt::Horizontal;
    opt.position         = QStyleOptionHeader::Middle;
    opt.section          = 1;
    opt.selectedPosition = QStyleOptionHeader::NotAdjacent;
    opt.sortIndicator    = QStyleOptionHeader::None;
    opt.text             = str;
    opt.textAlignment    = align;

    p->save();
    if (bold)
        opt.state |= QStyle::State_On;
    style()->drawControl(QStyle::CE_Header, &opt, p, this);
    p->restore();
}

void PriceGuideWidget::paintCell(QPainter *p, const QRect &r, Qt::Alignment align, const QString &str, bool alternate)
{
    p->fillRect(r, palette().color(alternate ? QPalette::AlternateBase : QPalette::Base));

    QRect r2(r);
    r2.adjust(hborder, 0, -hborder, 0);

    p->setPen(palette().color(QPalette::Text));
    p->drawText(r2, align, str);
}

void PriceGuideWidget::paintEvent(QPaintEvent *e)
{
    QFrame::paintEvent(e);

    const QSize s = contentsRect().size();
    const QPoint offset = contentsRect().topLeft();

    QPalette pal = palette();
    QPainter p(this);

    p.fillRect(contentsRect(), pal.base());
    p.translate(offset.x(), offset.y());

    bool valid = d->m_pg && d->m_pg->valid();
    bool is_updating = d->m_pg && (d->m_pg->updateStatus() == BrickLink::Updating);

    QString str = d->m_pg ? "-" : "";

    for (QList<cell>::const_iterator it = d->m_cells.begin(); it != d->m_cells.end(); ++it) {
        const cell &c = *it;

        if ((e->rect() & c).isEmpty())
            continue;

        switch (c.m_type) {
        case cell::Header:
            paintHeader(&p, c, c.m_text_flags, c.m_text == "$$$" ? Currency::symbol() : c.m_text, c.m_flag);
            break;

        case cell::Quantity:
            if (!is_updating) {
                if (valid)
                    str = QString("%1 (%2)").arg(d->m_pg->quantity(c.m_time, c.m_condition)).arg(d->m_pg->lots(c.m_time, c.m_condition));

                paintCell(&p, c, c.m_text_flags, str, c.m_flag);
            }
            break;

        case cell::Price:
            if (!is_updating) {
                if (valid)
                    str = d->m_pg->price(c.m_time, c.m_condition, c.m_price).toLocal();

                paintCell(&p, c, c.m_text_flags, str, c.m_flag);
            }
            break;

        case cell::Update:
            if (is_updating)
                paintCell(&p, c, c.m_text_flags, d->m_str_wait, c.m_flag);
            break;

        case cell::Empty:
            p.fillRect(c, pal.base());
            break;
        }
    }
}

void PriceGuideWidget::mouseDoubleClickEvent(QMouseEvent *me)
{
    if (!d->m_pg)
        return;

    QList<cell>::const_iterator it = d->cellAtPos(me->pos());

    if ((it != d->m_cells.end()) && ((*it).m_type == cell::Price))
        emit priceDoubleClicked(d->m_pg->price((*it).m_time, (*it).m_condition, (*it).m_price));
}

void PriceGuideWidget::mouseMoveEvent(QMouseEvent *me)
{
    if (!d->m_pg)
        return;

    QList<cell>::const_iterator it = d->cellAtPos(me->pos());

    d->m_on_price = ((it != d->m_cells.end()) && ((*it).m_type == cell::Price));

    if (d->m_on_price)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void PriceGuideWidget::leaveEvent(QEvent * /*e*/)
{
    if (d->m_on_price) {
        d->m_on_price = false;
        unsetCursor();
    }
}

bool PriceGuideWidget::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        QHelpEvent *he = static_cast<QHelpEvent *>(e);
        QList<cell>::const_iterator it = d->cellAtPos(he->pos());

        if ((it != d->m_cells.end()) && ((*it).m_type == cell::Price))
            QToolTip::showText(he->globalPos(), PriceGuideWidget::tr("Double-click to set the price of the current item."), this);
        e->accept();
        return true;
    }
    else
        return QWidget::event(e);
}

QRegion PriceGuideWidget::nonStaticCells() const
{
    QRegion r;

    for (QList<cell>::const_iterator it = d->m_cells.begin(); it != d->m_cells.end(); ++it) {
        const cell &c = *it;

        switch (c.m_type) {
        case cell::Quantity:
        case cell::Price   :
        case cell::Update  : r |= c; break;
        default            : break;
        }
    }
    return r;
}
