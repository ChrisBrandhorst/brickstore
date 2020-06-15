/* Copyright (C) 2013-2014 Patrick Brans.  All rights reserved.
**
** This file is part of BrickStock.
** BrickStock is based heavily on BrickStore (http://www.brickforge.de/software/brickstore/)
** by Robert Griebl, Copyright (C) 2004-2008.
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
#ifndef __CITEMVIEW_H__
#define __CITEMVIEW_H__

#include "clistview.h"
#include "cdocument.h"
#include "bricklink.h"
//Added by qt3to4:
#include <QPixmap>
#include <QEvent>
#include <qtooltip.h>
#include <q3header.h>

class QValidator;

class CItemViewItem;
class CItemViewPrivate;

class CItemView : public CListView {
	Q_OBJECT
public:
	CItemView ( CDocument *doc, QWidget *parent = 0, const char *name = 0 );
	virtual ~CItemView ( );

	CDocument *document ( ) const;

	enum Filter {
		All        = -1,

		Prices     = -2,
		Texts      = -3,
		Quantities = -4,

		FilterCountSpecial = 4
	};


	class EditComboItem {
	public:
		EditComboItem ( const QString &str = QString::null )
			: m_text ( str ), m_id ( -1 ) { }
		EditComboItem ( const QString &str, const QColor &fg, const QColor &bg, int id )
			: m_text ( str ), m_fg ( fg ), m_bg ( bg ), m_id ( id ) { }

	protected:
		QString m_text;
		QColor  m_fg;
		QColor  m_bg;
		int     m_id;

		friend class CItemView;
	};

	void editWithLineEdit ( CItemViewItem *ivi, int col, const QString &text, const QString &mask = QString::null, QValidator *valid = 0, const QString &empty_value = QString ( ));

	bool isDifferenceMode ( ) const;
	bool isSimpleMode ( ) const;

	QWidget *createFilterWidget(QWidget *parent);

public slots:
	void setDifferenceMode ( bool b );
	void setSimpleMode ( bool b );

	void cancelEdit ( );
	void terminateEdit ( bool commit );
	void applyFilter ( const QString &filter, int field, bool is_regex );
	
	void loadDefaultLayout ( );
	void saveDefaultLayout ( );

signals:
	void editDone ( CItemViewItem *ivi, int col, const QString &text, bool valid );
	void editCanceled ( CItemViewItem *ivi, int col );

protected:
	void edit ( CItemViewItem *ivi, int col, QWidget *editor );
	bool eventFilter ( QObject *o, QEvent *e );

protected slots:
	virtual void listItemDoubleClicked ( Q3ListViewItem *, const QPoint &, int );
	void applyFilterInternal ( );
	void languageChange ( );

protected:
	static QString statusLabel ( BrickLink::InvItem::Status status );
	static QString conditionLabel ( BrickLink::Condition cond );
	static QString subConditionLabel ( BrickLink::SubCondition scond );

	static bool filterExpressionMatch ( const QRegExp &regxep, int comp, const QString &str );

private:
	CItemViewPrivate *d;

	friend class CItemViewItem;
};


class CItemViewItem : public CListViewItem {
public:
	CItemViewItem ( CDocument::Item *item, Q3ListViewItem *parent, Q3ListViewItem *after );
	CItemViewItem ( CDocument::Item *item, Q3ListView *parent, Q3ListViewItem *after );

	virtual ~CItemViewItem ( );

	enum { RTTI = 1000 };

	virtual int width ( const QFontMetrics &fm, const Q3ListView *lv, int c ) const;
	virtual QString text ( int column ) const;
	virtual const QPixmap *pixmap ( int column ) const;
	virtual QString key ( int column, bool ascending ) const;
	virtual int compare ( Q3ListViewItem * i, int col, bool ascending ) const;
	virtual void setup ();
	virtual void paintCell ( QPainter * p, const QColorGroup & cg, int column, int width, int align );
	virtual int rtti () const;
	virtual QString toolTip ( int column ) const;

	CDocument::Item *item ( ) const;
	BrickLink::Picture *picture ( ) const;

	virtual void doubleClicked ( const QPoint &p, int col );

	virtual CItemView *listView ( ) const;

	virtual void editDone ( int col, const QString &result, bool valid );

	QRect globalRect ( int col ) const;

protected:
	QColor shadeColor ( int index ) const;

private:
	void init ( CDocument::Item *item );

	CDocument::Item *           m_item;
	mutable BrickLink::Picture *m_picture;
	Q_UINT64                    m_truncated;

    friend class CItemViewToolTip;
};

class CItemViewToolTip : public QObject {
    Q_OBJECT

public:
    CItemViewToolTip ( QWidget *parent, CItemView *iv )
        : QObject( parent ), m_iv ( iv )
    { }

    virtual ~CItemViewToolTip ( )
    { }

    bool eventFilter( QObject *, QEvent *e )
    {
        bool res = false;
        if ( !(QWidget *)QObject::parent() || !m_iv /*|| !m_iv-> showToolTips ( )*/ )
            res = false;

        if (e->type() == QEvent::ToolTip) {
            QToolTip::remove ( (QWidget *)QObject::parent());
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(e);
            QPoint pos = helpEvent->pos();

            CItemViewItem *item = static_cast <CItemViewItem *> ( m_iv-> itemAt ( pos ));
            QPoint contentpos = m_iv-> viewportToContents ( pos );

            if ( item ) {
                int col = m_iv-> header ( )-> sectionAt ( contentpos. x ( ));
                QString text = item-> toolTip ( col );

                if ( text. isEmpty ( ))
                    return false;

                QRect r = m_iv-> itemRect ( item );
                int headerleft = m_iv-> header ( )-> sectionPos ( col ) - m_iv-> contentsX ( );
                r. setLeft ( headerleft );
                r. setRight ( headerleft + m_iv-> header ( )-> sectionSize ( col ));

                QToolTip::showText(helpEvent->globalPos(), text, (QWidget *)QObject::parent(), r);
            }

            res = true;
        }

        return res;
    }

private:
    CItemView *m_iv;
};

#endif
