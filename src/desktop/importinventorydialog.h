/* Copyright (C) 2004-2022 Robert Griebl. All rights reserved.
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

#include <QDialog>

#include "ui_importinventorydialog.h"

QT_FORWARD_DECLARE_CLASS(QPushButton)


class ImportInventoryDialog : public QDialog, private Ui::ImportInventoryDialog
{
    Q_OBJECT
public:
    ImportInventoryDialog(QWidget *parent = nullptr);
    ~ImportInventoryDialog() override;

    bool setItem(const BrickLink::Item *item);
    const BrickLink::Item *item() const;
    int quantity() const;
    BrickLink::Condition condition() const;
    BrickLink::Status extraParts() const;
    bool includeInstructions() const;

protected:
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    QSize sizeHint() const override;

    QByteArray saveState() const;
    bool restoreState(const QByteArray &ba);
    void languageChange();

protected slots:
    void checkItem(const BrickLink::Item *it, bool ok);
    void importInventory();

private:
    QPushButton *w_import;
};
