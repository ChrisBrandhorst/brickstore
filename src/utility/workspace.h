/* Copyright (C) 2004-2020 Robert Griebl. All rights reserved.
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
#pragma once

#include <QWidget>

class TabBar;
class QStackedLayout;
class QMenu;
class QTextDocument;


class Workspace : public QWidget
{
    Q_OBJECT
public:
    Workspace(QWidget *parent = nullptr);

    QString backgroundText() const;
    void setBackgroundText(const QString &text);

    void addWindow(QWidget *w);

    QWidget *activeWindow() const;
    int windowCount() const;
    QList<QWidget *> windowList() const;

    QMenu *windowMenu(bool hasShortcuts = false, QWidget *parent = nullptr);

signals:
    void windowActivated(QWidget *);
    void windowCountChanged(int count);

public slots:
    void setActiveWindow(QWidget *);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void closeTab(int idx);
    void activateTab(int idx);
    void moveTab(int from, int to);
    void removeTab(int idx);

private:
    TabBar *        m_tabbar;
    QStackedLayout *m_windowStack;
    QWidget *       m_right;
    QString         m_backgroundText;
    QTextDocument * m_backgroundTextDocument = nullptr;
};