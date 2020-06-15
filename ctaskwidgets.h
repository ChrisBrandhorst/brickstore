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
#ifndef __CTASKWIDGETS_H__
#define __CTASKWIDGETS_H__

#include <QStackedWidget>
//Added by qt3to4:
#include <QEvent>
#include <QLabel>

#include "cdocument.h"
#include "cappearsinwidget.h"
#include "cpriceguidewidget.h"
#include "cpicturewidget.h"
#include "curllabel.h"

class QLabel;


class CTaskLinksWidget : public CUrlLabel {
	Q_OBJECT

public:
	CTaskLinksWidget ( QWidget *parent, const char *name = 0 );

protected slots:
	void documentUpdate ( CDocument *doc );
	void selectionUpdate ( const CDocument::ItemList &list );

	void languageChange ( );

private:
	CDocument * m_doc;

};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------


class CTaskPriceGuideWidget : public CPriceGuideWidget {
	Q_OBJECT

public:
	CTaskPriceGuideWidget ( QWidget *parent, const char *name = 0 );

protected slots:
	void documentUpdate ( CDocument *doc );
	void selectionUpdate ( const CDocument::ItemList &list );

	void setPrice ( money_t p );

protected:
	virtual bool event ( QEvent *e );
	void fixParentDockWindow ( );

private slots:
	void setOrientation ( Qt::Orientation o );

private:
	CDocument *m_doc;
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------


class CTaskInfoWidget : public QStackedWidget {
	Q_OBJECT

public:
    CTaskInfoWidget (QWidget *parent, const char *name = 0);

protected slots:
	void documentUpdate ( CDocument *doc );
	void selectionUpdate ( const CDocument::ItemList &list );

	void languageChange ( );
	void refresh ( );

	virtual void paletteChange ( const QPalette &oldpal );

private:
	QLabel *        m_text;
	CPictureWidget *m_pic;
	CDocument *     m_doc;
};


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

class CTaskAppearsInWidget : public CAppearsInWidget {
	Q_OBJECT

public:
	CTaskAppearsInWidget ( QWidget *parent, const char *name = 0 );

	virtual QSize minimumSizeHint ( ) const;
	virtual QSize sizeHint ( ) const;

protected slots:
	void documentUpdate ( CDocument *doc );
	void selectionUpdate ( const CDocument::ItemList &list );

private:
	CDocument * m_doc;
};

#endif
