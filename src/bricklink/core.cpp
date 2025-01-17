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
#include <cstdio>
#include <cstdlib>

#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QDebug>
#include <QThread>
#include <QUrlQuery>
#include <QStringBuilder>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRunnable>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#  include <qhashfunctions.h>
#  define q5Hash qHash
#else
#  include "utility/q5hashfunctions.h"
#endif
#include "utility/utility.h"
#include "utility/systeminfo.h"
#include "utility/stopwatch.h"
#include "utility/exception.h"
#include "utility/transfer.h"

#include "bricklink/category.h"
#include "bricklink/changelogentry.h"
#include "bricklink/color.h"
#include "bricklink/core.h"
#include "bricklink/item.h"
#include "bricklink/itemtype.h"
#include "bricklink/lot.h"
#include "bricklink/partcolorcode.h"
#include "bricklink/picture.h"
#include "bricklink/priceguide.h"
#if !defined(BS_BACKEND)
#  include "bricklink/cart.h"
#  include "bricklink/order.h"
#  include "bricklink/store.h"
#endif


namespace BrickLink {

void Core::openUrl(UrlList u, const void *opt, const void *opt2)
{
    QUrl url;

    switch (u) {
    case URL_InventoryRequest:
        url = "https://www.bricklink.com/catalogInvAdd.asp"_l1;
        break;

    case URL_WantedListUpload:
        url = "https://www.bricklink.com/wantedXML.asp"_l1;
        break;

    case URL_InventoryUpload:
        url = "https://www.bricklink.com/invXML.asp"_l1;
        break;

    case URL_InventoryUpdate:
        url = "https://www.bricklink.com/invXML.asp#update"_l1;
        break;

    case URL_CatalogInfo: {
        auto item = static_cast<const Item *>(opt);
        if (item && item->itemType()) {
            url = "https://www.bricklink.com/catalogItem.asp"_l1;
            QUrlQuery query;
            query.addQueryItem(QString(QLatin1Char(item->itemTypeId())), QLatin1String(item->id()));
            if (item->itemType()->hasColors() && opt2)
                query.addQueryItem("C"_l1, QString::number(static_cast<const Color *>(opt2)->id()));
            url.setQuery(query);
        }
        break;
    }
    case URL_PriceGuideInfo: {
        auto *item = static_cast<const Item *>(opt);
        if (item && item->itemType()) {
            url = "https://www.bricklink.com/catalogPG.asp"_l1;
            QUrlQuery query;
            query.addQueryItem(QString(QLatin1Char(item->itemTypeId())), Utility::urlQueryEscape(item->id()));
            if (item->itemType()->hasColors() && opt2)
                query.addQueryItem("colorID"_l1, QString::number(static_cast<const Color *>(opt2)->id()));
            url.setQuery(query);
        }
        break;
    }
    case URL_LotsForSale: {
        auto item = static_cast<const Item *>(opt);
        if (item && item->itemType()) {
            url = "https://www.bricklink.com/search.asp"_l1;
            QUrlQuery query;
            query.addQueryItem("viewFrom"_l1, "sa"_l1);
            query.addQueryItem("itemType"_l1, QString(QLatin1Char(item->itemTypeId())));

            // workaround for BL not accepting the -X suffix for sets, instructions and boxes
            QString id = QLatin1String(item->id());
            char itt = item->itemTypeId();

            if (itt == 'S' || itt == 'I' || itt == 'O') {
                int pos = id.lastIndexOf('-'_l1);
                if (pos >= 0)
                    id.truncate(pos);
            }
            query.addQueryItem("q"_l1, Utility::urlQueryEscape(id));

            if (item->itemType()->hasColors() && opt2)
                query.addQueryItem("colorID"_l1, QString::number(static_cast<const Color *>(opt2)->id()));
            url.setQuery(query);
        }
        break;
    }
    case URL_AppearsInSets: {
        auto item = static_cast<const Item *>(opt);
        if (item && item->itemType()) {
            url = "https://www.bricklink.com/catalogItemIn.asp"_l1;
            QUrlQuery query;
            query.addQueryItem(QString(QLatin1Char(item->itemTypeId())), Utility::urlQueryEscape(item->id()));
            query.addQueryItem("in"_l1, "S"_l1);

            if (item->itemType()->hasColors() && opt2)
                query.addQueryItem("colorID"_l1, QString::number(static_cast<const Color *>(opt2)->id()));
            url.setQuery(query);
        }
        break;
    }
    case URL_ColorChangeLog:
        url = "https://www.bricklink.com/catalogReqList.asp?pg=1&chgUserID=&viewActionType=R"_l1;
        break;

    case URL_ItemChangeLog: {
        url = "https://www.bricklink.com/catalogReqList.asp?pg=1&chgUserID=&viewActionType=I"_l1;
        QUrlQuery query;
        if (opt)
            query.addQueryItem("q"_l1, Utility::urlQueryEscape(static_cast<const char *>(opt)));
        url.setQuery(query);
        break;
    }
    case URL_StoreItemDetail: {
        auto lotId = static_cast<const unsigned int *>(opt);
        if (lotId && *lotId) {
            url = "https://www.bricklink.com/inventory_detail.asp"_l1;
            QUrlQuery query;
            query.addQueryItem("invID"_l1, Utility::urlQueryEscape(QString::number(*lotId)));
            url.setQuery(query);
        }
        break;
    }
    case URL_StoreItemSearch: {
        const Item *item = static_cast<const Item *>(opt);
        const Color *color = static_cast<const Color *>(opt2);
        if (item && item->itemType()) {
            url = "https://www.bricklink.com/inventory_detail.asp?"_l1;
            QUrlQuery query;
            query.addQueryItem("catType"_l1, QString(QLatin1Char(item->itemTypeId())));
            QString queryTerm = QLatin1String(item->id());
            if (queryTerm.contains('-'_l1)) {
                queryTerm = item->name();
                queryTerm.remove('('_l1);
                queryTerm.remove(')'_l1);
            }
            query.addQueryItem("q"_l1, Utility::urlQueryEscape(queryTerm));
            if (item->itemType()->hasColors() && color)
                query.addQueryItem("ColorID"_l1, QString::number(color->id()));
            url.setQuery(query);
        }
        break;
    }
    case URL_OrderDetails: {
        auto orderId = static_cast<const char *>(opt);
        if (orderId && *orderId) {
            url = "https://www.bricklink.com/orderDetail.asp"_l1;
            QUrlQuery query;
            query.addQueryItem("ID"_l1, Utility::urlQueryEscape(orderId));
            url.setQuery(query);
        }
        break;
    }
    case URL_ShoppingCart: {
        auto shopId = static_cast<const int *>(opt);
        if (shopId && *shopId) {
            url = "https://www.bricklink.com/v2/globalcart.page"_l1;
            QUrlQuery query;
            query.addQueryItem("sid"_l1, Utility::urlQueryEscape(QString::number(*shopId)));
            url.setQuery(query);
        }
        break;
    }
    }
    if (url.isValid())
        QDesktopServices::openUrl(url);
}


const QImage Core::noImage(const QSize &s) const
{
    uint key = uint(s.width() << 16) | uint(s.height());
    QImage img = m_noImageCache.value(key);

    if (img.isNull()) {
        img = m_noImageIcon.pixmap(s).toImage();
        m_noImageCache.insert(key, img);
    }
    return img;
}


const QImage Core::colorImage(const Color *col, int w, int h) const
{
    if (!col || w <= 0 || h <= 0)
        return QImage();

    //TODO for Qt6: use more than 10/11/11 bits ... qHash is 64bit wide now
    uint key = uint(col->id() << 22) | uint(w << 11) | uint(h);
    QImage img = m_colorImageCache.value(key);

    if (img.isNull()) {
        QColor c = col->color();

        img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing);
        QRect r = img.rect();

        QBrush brush;

        if (col->isGlitter()) {
            brush = QBrush(Utility::contrastColor(c, 0.25), Qt::Dense6Pattern);
        }
        else if (col->isSpeckle()) {
            // hack for speckled colors
            QColor c2;

            if (!c.isValid()) {
                QString name = col->name();
                int dash = name.indexOf('-'_l1);
                if (dash > 0) {
                    QString basename = name.mid(8, dash - 8);
                    if (basename.startsWith("DB"_l1))
                        basename.replace(0, 2, "Dark Bluish "_l1);
                    QString speckname = name.mid(dash + 1);

                    const Color *basec = colorFromName(basename);
                    const Color *speckc = colorFromName(speckname);

                    if (basec)
                        c = basec->color();
                    if (speckc)
                        c2 = speckc->color();
                }
            }
            if (c.isValid()) {
                if (!c2.isValid()) // fake
                    c2 = Utility::contrastColor(c, 0.2);
                brush = QBrush(c2, Qt::Dense7Pattern);
            }
        }
        else if (col->isMetallic()) {
            QColor c2 = Utility::gradientColor(c, Qt::black);

            QLinearGradient gradient(0, 0, 0, r.height());
            gradient.setColorAt(0,   c2);
            gradient.setColorAt(0.3, c);
            gradient.setColorAt(0.7, c);
            gradient.setColorAt(1,   c2);
            brush = gradient;
        }
        else if (col->isChrome()) {
            QColor c2 = Utility::gradientColor(c, Qt::black);

            QLinearGradient gradient(0, 0, r.width(), 0);
            gradient.setColorAt(0,   c2);
            gradient.setColorAt(0.3, c);
            gradient.setColorAt(0.7, c);
            gradient.setColorAt(1,   c2);
            brush = gradient;
        }

        if (c.isValid()) {
            p.fillRect(r, c);
            p.fillRect(r, brush);
        }
        else {
            p.fillRect(0, 0, w, h, Qt::white);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::darkGray);
            p.setBrush(QColor(255, 255, 255, 128));
            p.drawRect(r);
            p.drawLine(0, 0, w, h);
            p.drawLine(0, h, w, 0);
        }

        if (col->isTransparent()) {
            QLinearGradient gradient(0, 0, r.width(), r.height());
            gradient.setColorAt(0,   Qt::transparent);
            gradient.setColorAt(0.4, Qt::transparent);
            gradient.setColorAt(0.6, QColor(255, 255, 255, 100));
            gradient.setColorAt(1,   QColor(255, 255, 255, 100));

            p.fillRect(r, gradient);
        }
        p.end();

        m_colorImageCache.insert(key, img);
    }
    return img;
}


QString Core::dataPath() const
{
    return m_datadir;
}

QString Core::dataFileName(QStringView fileName, const Item *item, const Color *color) const
{
    // Avoid huge directories with 1000s of entries.
    // sse4.2 is only used if a seed value is supplied
    // please note: Qt6's qHash is incompatible
    uchar hash = q5Hash(QString::fromLatin1(item->id()), 42) & 0xff;

    QString p = m_datadir % QLatin1Char(item->itemTypeId()) % u'/' % (hash < 0x10 ? u"0" : u"")
            % QString::number(hash, 16) % u'/' % QLatin1String(item->id()) % u'/'
            % (color ? QString::number(color->id()) : QString()) % (color ? u"/" : u"")
            % fileName;

    return p;
}

QFile *Core::dataReadFile(QStringView fileName, const Item *item, const Color *color) const
{
    auto f = new QFile(dataFileName(fileName, item, color));
    f->open(QIODevice::ReadOnly);
    return f;
}

QSaveFile *Core::dataSaveFile(QStringView fileName, const Item *item, const Color *color) const
{
    auto p = dataFileName(fileName, item, color);

    if (!QDir(fileName.isEmpty() ? p : p.left(p.size() - int(fileName.size()))).mkpath("."_l1))
        return nullptr;

    auto f = new QSaveFile(p);
    if (!f->open(QIODevice::WriteOnly)) {
        qWarning() << "BrickLink::Core::dataSaveFile failed to open" << f->fileName()
                   << "for writing:" << f->errorString();
    }
    return f;
}

void Core::setCredentials(const QPair<QString, QString> &credentials)
{
    if (m_credentials != credentials) {
        bool wasAuthenticated = m_authenticated;
        m_authenticated = false;
        if (m_authenticatedTransfer)
            m_authenticatedTransfer->abortAllJobs();
        bool newUserId = (credentials.first != m_credentials.first);
        m_credentials = credentials;
        if (wasAuthenticated) {
            emit authenticationChanged(false);

            QUrl url("https://www.bricklink.com/ajax/renovate/loginandout.ajax"_l1);
            QUrlQuery q;
            q.addQueryItem("do_logout"_l1, "true"_l1);
            url.setQuery(q);

            auto logoutJob = TransferJob::get(url);
            m_authenticatedTransfer->retrieve(logoutJob, true);
        }
        if (newUserId)
            emit userIdChanged(m_credentials.first);
    }
}

QString Core::userId() const
{
    return m_credentials.first;
}

bool Core::isAuthenticated() const
{
    return m_authenticated;
}

void Core::retrieveAuthenticated(TransferJob *job)
{
    job->setNoRedirects(true);

    if (!m_authenticated) {
        if (!m_loginJob) {
            QUrl url("https://www.bricklink.com/ajax/renovate/loginandout.ajax"_l1);
            QUrlQuery q;
            q.addQueryItem("userid"_l1,          Utility::urlQueryEscape(m_credentials.first));
            q.addQueryItem("password"_l1,        Utility::urlQueryEscape(m_credentials.second));
            q.addQueryItem("keepme_loggedin"_l1, "1"_l1);
            url.setQuery(q);

            m_loginJob = TransferJob::post(url, nullptr, true /* no redirects */);
            m_authenticatedTransfer->retrieve(m_loginJob, true);
        }
        m_jobsWaitingForAuthentication << job;
    } else {
        m_authenticatedTransfer->retrieve(job);
    }
}


Core *Core::s_inst = nullptr;

Core *Core::create(const QString &datadir, QString *errstring)
{
    if (!s_inst) {
        s_inst = new Core(datadir);
#if !defined(BS_BACKEND)
        s_inst->m_database = new Database(s_inst);
        s_inst->m_store = new Store(s_inst);
        s_inst->m_orders = new Orders(s_inst);
        s_inst->m_carts = new Carts(s_inst);
#endif

        QString test = s_inst->dataPath();

        if (!test.isEmpty()) {
            QFileInfo fi(test);

            if (!fi.exists()) {
                QDir(test).mkpath("."_l1);
                fi.refresh();
            }
            if (!fi.exists() || !fi.isDir() || !fi.isReadable() || !fi.isWritable())
                test.clear();
        }

        if (test.isEmpty()) {
            delete s_inst;
            s_inst = nullptr;

            if (errstring)
                *errstring = tr("Data directory \'%1\' is not both read- and writable.").arg(datadir);
        }
    }
    return s_inst;
}

Core::Core(const QString &datadir)
    : m_datadir(QDir::cleanPath(QDir(datadir).absolutePath()) + u'/')
    , m_noImageIcon(QIcon::fromTheme("image-missing-large"_l1))
    , m_transfer(new Transfer(this))
    , m_authenticatedTransfer(new Transfer(this))
{
    connect(m_transfer, &Transfer::finished,
            this, [this](TransferJob *job) {
        if (job) {
            if (Picture *pic = job->userData("picture").value<Picture *>())
                pictureJobFinished(job, pic);
            else if (PriceGuide *pg = job->userData("priceGuide").value<PriceGuide *>())
                priceGuideJobFinished(job, pg);
        }
    });
    connect(m_transfer, &Transfer::overallProgress,
            this, &Core::transferProgress);

    connect(m_authenticatedTransfer, &Transfer::finished,
            this, [this](TransferJob *job) {
        if (!job) {
            return;
        } else if (job == m_loginJob) {
            m_loginJob = nullptr;

            QString error;

            if (job->responseCode() == 200 && job->data()) {
                auto json = QJsonDocument::fromJson(*job->data());
                if (!json.isNull()) {
                    bool wasAuthenticated = m_authenticated;
                    m_authenticated = (json.object().value("returnCode"_l1).toInt() == 0);
                    if (!m_authenticated)
                        error = json.object().value("returnMessage"_l1).toString();

                    if (wasAuthenticated != m_authenticated)
                        emit authenticationChanged(m_authenticated);
                }
            } else {
                error = job->errorString();
            }
            if (!error.isEmpty())
                emit authenticationFailed(m_credentials.first, error);

            for (TransferJob *authJob : qAsConst(m_jobsWaitingForAuthentication))
                m_authenticatedTransfer->retrieve(authJob);
            m_jobsWaitingForAuthentication.clear();

            if (!m_authenticated)
                m_authenticatedTransfer->abortAllJobs();
        } else {
            if (job->responseCode() == 302 &&
                    (job->redirectUrl().toString().contains("v2/login.page"_l1)
                     || job->redirectUrl().toString().contains("login.asp?"_l1))) {
                m_authenticated = false;
                emit authenticationChanged(m_authenticated);

                job->resetForReuse();
                QMetaObject::invokeMethod(this, [=, this]() {
                    retrieveAuthenticated(job);
                }, Qt::QueuedConnection);
            } else {
                emit authenticatedTransferFinished(job);
            }
        }
    });
    connect(m_authenticatedTransfer, &Transfer::overallProgress,
            this, &Core::authenticatedTransferOverallProgress);
    connect(m_authenticatedTransfer, &Transfer::progress,
            this, &Core::authenticatedTransferProgress);
    connect(m_authenticatedTransfer, &Transfer::started,
            this, &Core::authenticatedTransferStarted);

    m_diskloadPool.setMaxThreadCount(QThread::idealThreadCount() * 3);
    m_online = true;

    // the max. pic cache size is at least 1GB and at max half the physical memory on 64bit systems
    // the max. pg cache size is at least 5.000 and 10.000 if more than 3GB of RAM are available
    quint64 picCacheMem = 1'000'000'000ULL;
    int pgCacheEntries = 5'000;

#if Q_PROCESSOR_WORDSIZE >= 8
    picCacheMem = qMax(picCacheMem, SystemInfo::inst()->physicalMemory() / 2);
    if (SystemInfo::inst()->physicalMemory() >= 3'000'000'000ULL)
        pgCacheEntries *= 2;
#endif

    m_pic_cache.setMaxCost(int(picCacheMem / 1024)); // each pic has the cost of memory used in KB
    m_pg_cache.setMaxCost(pgCacheEntries); // each priceguide has a cost of 1
}

Core::~Core()
{
    clear();
    s_inst = nullptr;
}

void Core::setUpdateIntervals(const QMap<QByteArray, int> &intervals)
{
    m_database->m_updateInterval = intervals["Database"];
    m_pic_update_iv = intervals["Picture"];
    m_pg_update_iv = intervals["PriceGuide"];
}

bool Core::updateNeeded(bool valid, const QDateTime &last, int iv)
{
    return (iv > 0) && (!valid || (last.secsTo(QDateTime::currentDateTime()) > iv));
}

void Core::setOnlineStatus(bool isOnline)
{
    m_online = isOnline;
    if (!isOnline)
        cancelTransfers();
}

bool Core::onlineStatus() const
{
    return m_online;
}

QString Core::countryIdFromName(const QString &name) const
{
    // BrickLink doesn't use the standard ISO country names...
    static const char * const brickLinkCountries[] = {
        "AF Afghanistan",
        "AL Albania",
        "DZ Algeria",
        "AD Andorra",
        "AO Angola",
        "AI Anguilla",
        "AG Antigua and Barbuda",
        "AR Argentina",
        "AM Armenia",
        "AW Aruba",
        "AU Australia",
        "AT Austria",
        "AZ Azerbaijan",
        "BS Bahamas",
        "BH Bahrain",
        "BD Bangladesh",
        "BB Barbados",
        "BY Belarus",
        "BE Belgium",
        "BZ Belize",
        "BJ Benin",
        "BM Bermuda",
        "BT Bhutan",
        "BO Bolivia",
        "BA Bosnia and Herzegovina",
        "BW Botswana",
        "BR Brazil",
        "IO British Indian Ocean Territory",
        "BN Brunei",
        "BG Bulgaria",
        "BF Burkina Faso",
        "BI Burundi",
        "KH Cambodia",
        "CM Cameroon",
        "CA Canada",
        "CV Cape Verde",
        "BQ Caribbean Netherlands",
        "KY Cayman Islands",
        "CF Central African Republic",
        "TD Chad",
        "CL Chile",
        "CN China",
        "CO Colombia",
        "KM Comoros",
        "CG Congo",
        "CD Congo (DRC)",
        "CK Cook Islands",
        "CR Costa Rica",
        "CI Cote D\'Ivoire",
        "HR Croatia",
        "CW Curacao",
        "CY Cyprus",
        "CZ Czech Republic",
        "DK Denmark",
        "DJ Djibouti",
        "DM Dominica",
        "DO Dominican Republic",
        "TL East Timor",
        "EC Ecuador",
        "EG Egypt",
        "SV El Salvador",
        "GQ Equatorial Guinea",
        "ER Eritrea",
        "EE Estonia",
        "ET Ethiopia",
        "FK Falkland Islands (Islas Malvinas)",
        "FO Faroe Islands",
        "FJ Fiji",
        "FI Finland",
        "FR France",
        "PF French Polynesia",
        "GA Gabon",
        "GM Gambia",
        "GE Georgia",
        "DE Germany",
        "GH Ghana",
        "GI Gibraltar",
        "GR Greece",
        "GL Greenland",
        "GD Grenada",
        "GT Guatemala",
        "GN Guinea",
        "GW Guinea-Bissau",
        "GY Guyana",
        "HT Haiti",
        "HN Honduras",
        "HK Hong Kong",
        "HU Hungary",
        "IS Iceland",
        "IN India",
        "ID Indonesia",
        "IQ Iraq",
        "IE Ireland",
        "IL Israel",
        "IT Italy",
        "JM Jamaica",
        "JP Japan",
        "JO Jordan",
        "KZ Kazakhstan",
        "KE Kenya",
        "KI Kiribati",
        "KW Kuwait",
        "KG Kyrgyzstan",
        "LA Laos",
        "LV Latvia",
        "LB Lebanon",
        "LS Lesotho",
        "LR Liberia",
        "LI Liechtenstein",
        "LT Lithuania",
        "LU Luxembourg",
        "LY Lybia",
        "MO Macau",
        "MK Macedonia",
        "MG Madagascar",
        "MW Malawi",
        "MY Malaysia",
        "MV Maldives",
        "ML Mali",
        "MT Malta",
        "MH Marshall Islands",
        "MR Mauritania",
        "MU Mauritius",
        "YT Mayotte",
        "MX Mexico",
        "FM Micronesia",
        "MD Moldova",
        "MC Monaco",
        "MN Mongolia",
        "ME Montenegro",
        "MS Montserrat",
        "MA Morocco",
        "MZ Mozambique",
        "MM Myanmar",
        "NA Namibia",
        "NR Nauru",
        "NP Nepal",
        "NL Netherlands",
        "NC New Caledonia",
        "NZ New Zealand",
        "NI Nicaragua",
        "NE Niger",
        "NU Niue",
        "NF Norfolk Island",
        "NO Norway",
        "OM Oman",
        "PK Pakistan",
        "PW Palau",
        "PA Panama",
        "PG Papua new Guinea",
        "PY Paraguay",
        "PE Peru",
        "PH Philippines",
        "PN Pitcairn Islands",
        "PL Poland",
        "PT Portugal",
        "QA Qatar",
        "RO Romania",
        "RU Russia",
        "RW Rwanda",
        "WS Samoa",
        "SM San Marino",
        "ST Sao Tome and Principe",
        "SA Saudi Arabia",
        "SN Senegal",
        "RS Serbia",
        "SC Seychelles",
        "SL Sierra Leone",
        "SG Singapore",
        "SX Sint Maarten",
        "SK Slovakia",
        "SI Slovenia",
        "SB Solomon Islands",
        "SO Somalia",
        "ZA South Africa",
        "GS South Georgia",
        "KR South Korea",
        "ES Spain",
        "LK Sri Lanka",
        "SH St. Helena",
        "KN St. Kitts and Nevis",
        "LC St. Lucia",
        "PM St. Pierre and Miquelon",
        "VC St. Vincent and the Grenadines",
        "SD Sudan",
        "SR Suriname",
        "SJ Svalbard and Jan Mayen",
        "SZ Swaziland",
        "SE Sweden",
        "CH Switzerland",
        "TW Taiwan",
        "TJ Tajikistan",
        "TZ Tanzania",
        "TH Thailand",
        "TG Togo",
        "TO Tonga",
        "TT Trinidad and Tobago",
        "TN Tunisia",
        "TR Turkey",
        "TM Turkmenistan",
        "TC Turks and Caicos Islands",
        "TV Tuvalu",
        "UG Uganda",
        "UA Ukraine",
        "AE United Arab Emirates",
        "UK United Kingdom",
        "UY Uruguay",
        "US USA",
        "UZ Uzbekistan",
        "VU Vanuatu",
        "VA Vatican City State",
        "VE Venezuela",
        "VN Vietnam",
        "VG Virgin Islands (British)",
        "WF Wallis and Futuna",
        "YE Yemen",
        "ZM Zambia",
        "ZW Zimbabwe",
    };

    if (!name.isEmpty()) {
        for (const auto &country : brickLinkCountries) {
            QString countryStr = QString::fromLatin1(country);
            if (countryStr.mid(3) == name)
                return countryStr.left(2);
        }
    }
    return { };
}

const Category *Core::category(uint id) const
{
    auto it = std::lower_bound(categories().cbegin(), categories().cend(), id, &Category::lessThan);
    if ((it != categories().cend()) && (it->id() == id))
        return &(*it);
    return nullptr;
}

const Color *Core::color(uint id) const
{
    auto it = std::lower_bound(colors().cbegin(), colors().cend(), id, &Color::lessThan);
    if ((it != colors().cend()) && (it->id() == id))
        return &(*it);
    return nullptr;
}

const Color *Core::colorFromName(const QString &name) const
{
    if (name.isEmpty())
        return nullptr;

    auto it = std::find_if(colors().cbegin(), colors().cend(), [name](const auto &color) {
        return !color.name().compare(name, Qt::CaseInsensitive);
    });
    if (it != colors().cend())
        return &(*it);
    return nullptr;
}


const Color *Core::colorFromLDrawId(int ldrawId) const
{
    auto it = std::find_if(colors().cbegin(), colors().cend(), [ldrawId](const auto &color) {
        return (color.ldrawId() == ldrawId);
    });
    if (it != colors().cend())
        return &(*it);
    return nullptr;
}


const ItemType *Core::itemType(char id) const
{
    auto it = std::lower_bound(itemTypes().cbegin(), itemTypes().cend(), id, &ItemType::lessThan);
    if ((it != itemTypes().cend()) && (it->id() == id))
        return &(*it);
    return nullptr;
}

const Item *Core::item(char tid, const QByteArray &id) const
{
    auto needle = std::make_pair(tid, id);
    auto it = std::lower_bound(items().cbegin(), items().cend(), needle, Item::lessThan);
    if ((it != items().cend()) && (it->m_itemTypeId == tid) && (it->m_id == id))
        return &(*it);
    return nullptr;
}

const Item *Core::item(const std::string &tids, const QByteArray &id) const
{
    for (const char &tid : tids) {
        auto needle = std::make_pair(tid, id);
        auto it = std::lower_bound(items().cbegin(), items().cend(), needle, Item::lessThan);
        if ((it != items().cend()) && (it->m_itemTypeId == tid) && (it->m_id == id))
            return &(*it);
    }
    return nullptr;
}

const PartColorCode *Core::partColorCode(uint id)
{
    auto it = std::lower_bound(pccs().cbegin(), pccs().cend(), id, &PartColorCode::lessThan);
    if ((it != pccs().cend()) && (it->id() == id))
        return &(*it);
    return nullptr;
}

void Core::cancelTransfers()
{
    if (m_transfer)
        m_transfer->abortAllJobs();
    if (m_authenticatedTransfer)
        m_authenticatedTransfer->abortAllJobs();
    m_diskloadPool.clear();
    m_diskloadPool.waitForDone();
}

void Core::clear()
{
    cancelTransfers();

    m_pg_cache.clear();
    m_pic_cache.clear();

    database()->clear();
}

bool Core::applyChangeLog(const Item *&item, const Color *&color, Incomplete *inc)
{
    if (!inc)
        return false;

    if (!item) {
        QByteArray itemTypeAndId = inc->m_itemtype_id % inc->m_item_id;
        if (!inc->m_itemtype_name.isEmpty())
            itemTypeAndId[0] = inc->m_itemtype_name.at(0).toUpper().toLatin1();

        auto it = std::lower_bound(itemChangelog().cbegin(), itemChangelog().cend(), itemTypeAndId);
        if ((it != itemChangelog().cend()) && (*it == itemTypeAndId))
            item = core()->item(it->toItemTypeId(), it->toItemId());
    }
    if (!color) {
        uint colorId = inc->m_color_id;
        auto it = std::lower_bound(colorChangelog().cbegin(), colorChangelog().cend(), colorId);
        if ((it != colorChangelog().cend()) && (*it == colorId))
            color = core()->color(it->toColorId());
    }

    return (item && color);
}

qreal Core::itemImageScaleFactor() const
{
    return m_item_image_scale_factor;
}

void Core::setItemImageScaleFactor(qreal f)
{
    if (!qFuzzyCompare(f, m_item_image_scale_factor)) {
        m_item_image_scale_factor = f;

        m_noImageCache.clear();
        m_colorImageCache.clear();

        emit itemImageScaleFactorChanged(f);
    }
}

bool Core::isLDrawEnabled() const
{
    return !m_ldraw_datadir.isEmpty();
}

QString Core::ldrawDataPath() const
{
    return m_ldraw_datadir;
}

void Core::setLDrawDataPath(const QString &ldrawDataPath)
{
    m_ldraw_datadir = ldrawDataPath;
}

Core::ResolveResult Core::resolveIncomplete(Lot *lot)
{
    if (!lot->isIncomplete())
        return ResolveResult::Direct;

    Incomplete ic = *lot->isIncomplete();

    if ((ic.m_itemtype_id != ItemType::InvalidId) && !ic.m_item_id.isEmpty())
        lot->setItem(core()->item(ic.m_itemtype_id, ic.m_item_id));

    if (lot->item()) {
        if (auto *lic = lot->isIncomplete()) {
            lic->m_item_id.clear();
            lic->m_item_name.clear();
            lic->m_itemtype_id = ItemType::InvalidId;
            lic->m_itemtype_name.clear();
            lic->m_category_id = Category::InvalidId;
            lic->m_category_name.clear();
        }
    }

    if (ic.m_color_id != Color::InvalidId)
        lot->setColor(core()->color(ic.m_color_id));

    if (lot->color()) {
        if (auto *lic = lot->isIncomplete()) {
            lic->m_color_id = Color::InvalidId;
            lic->m_color_name.clear();
        }
    }

    if (lot->item() && lot->color()) {
        lot->setIncomplete(nullptr);
        return ResolveResult::Direct;

    } else {
        auto item = lot->item();
        auto color = lot->color();

        qWarning().noquote() << "Unknown item/color id:"
                             << (ic.m_itemtype_id ? QByteArray(1, ic.m_itemtype_id) : QByteArray("-"))
                             << ic.m_item_id << "@" << ic.m_color_id;

        bool ok = applyChangeLog(item, color, lot->isIncomplete());

        if (ok) {
            qWarning().noquote() << " > resolved via CL to:" << QByteArray(1, item->itemTypeId())
                                 << item->id() << "@" << color->id();
        }

        lot->setItem(item);
        lot->setColor(color);

        Q_ASSERT(ok == !lot->isIncomplete());
        return ok ? ResolveResult::ChangeLog : ResolveResult::Fail;
    }
}


class PriceGuideLoaderJob : public QRunnable
{
public:
    explicit PriceGuideLoaderJob(PriceGuide *pg)
        : QRunnable()
        , m_pg(pg)
    {
        pg->m_update_status = UpdateStatus::Loading;
    }

    void run() override;

    PriceGuide *priceGuide() const
    {
        return m_pg;
    }

private:
    Q_DISABLE_COPY(PriceGuideLoaderJob)

    PriceGuide *m_pg;
};

void PriceGuideLoaderJob::run()
{
    if (m_pg) {
        QDateTime fetched;
        PriceGuide::Data data;
        bool valid = m_pg->loadFromDisk(fetched, data);
        auto pg = m_pg;

        QMetaObject::invokeMethod(core(), [=]() {
            pg->m_valid = valid;
            pg->m_update_status = UpdateStatus::Ok;
            if (valid) {
                pg->m_fetched = fetched;
                pg->m_data = data;
            }
            core()->priceGuideLoaded(pg);
        }, Qt::QueuedConnection);
    }
}


PriceGuide *Core::priceGuide(const Item *item, const Color *color, bool highPriority)
{
    if (!item || !color)
        return nullptr;

    quint64 key = quint64(color->id()) << 32 | quint64(item->itemTypeId()) << 24 | quint64(item->index());
    PriceGuide *pg = m_pg_cache [key];

    bool needToLoad = false;

    if (!pg) {
        pg = new PriceGuide(item, color);
        if (!m_pg_cache.insert(key, pg)) {
            qWarning("Can not add priceguide to cache (cache max/cur: %d/%d, cost: %d)",
                     int(m_pg_cache.maxCost()), int(m_pg_cache.totalCost()), 1);
            return nullptr;
        }
        needToLoad = true;
    }

    if (highPriority) {
        if (!pg->isValid()) {
            pg->m_valid = pg->loadFromDisk(pg->m_fetched, pg->m_data);
            pg->m_update_status = UpdateStatus::Ok;
        }

        if (updateNeeded(pg->isValid(), pg->lastUpdated(), m_pg_update_iv))
            updatePriceGuide(pg, highPriority);
    }
    else if (needToLoad) {
        pg->addRef();
        m_diskloadPool.start(new PriceGuideLoaderJob(pg));
    }

    return pg;
}


void Core::priceGuideLoaded(PriceGuide *pg)
{
    if (pg) {
         if (pg->m_updateAfterLoad
                 || updateNeeded(pg->isValid(), pg->lastUpdated(), m_pg_update_iv))  {
             pg->m_updateAfterLoad = false;
             updatePriceGuide(pg, false);
         }
         emit priceGuideUpdated(pg);
         pg->release();
     }
}

void Core::updatePriceGuide(PriceGuide *pg, bool highPriority)
{
    if (!pg || (pg->m_update_status == UpdateStatus::Updating))
        return;

    if (!m_online || !m_transfer) {
        pg->m_update_status = UpdateStatus::UpdateFailed;
        emit priceGuideUpdated(pg);
        return;
    }

    if (pg->m_update_status == UpdateStatus::Loading) {
        pg->m_updateAfterLoad = true;
        return;
    }

    pg->m_update_status = UpdateStatus::Updating;
    pg->addRef();

    QUrlQuery query;
    QUrl url;

    if (pg->m_scrapedHtml) {
        url = QUrl("https://www.bricklink.com/priceGuideSummary.asp"_l1);
        query.addQueryItem("a"_l1,       QString(QLatin1Char(pg->item()->itemTypeId())));
        query.addQueryItem("vcID"_l1,    "1"_l1); // USD
        query.addQueryItem("vatInc"_l1,  "Y"_l1);
        query.addQueryItem("viewExclude"_l1, "Y"_l1);
        query.addQueryItem("ajView"_l1,  "Y"_l1); // only the AJAX snippet
        query.addQueryItem("colorID"_l1, QString::number(pg->color()->id()));
        query.addQueryItem("itemID"_l1,  Utility::urlQueryEscape(pg->item()->id()));
        query.addQueryItem("uncache"_l1, QString::number(QDateTime::currentMSecsSinceEpoch()));
    } else {
        //?{item type}={item no}&colorID={color ID}&cCode={currency code}&cExc={Y to exclude incomplete sets}
        url = QUrl("https://www.bricklink.com/BTpriceSummary.asp"_l1);

        query.addQueryItem(QString(QLatin1Char(pg->item()->itemTypeId())).toUpper(),
                           QLatin1String(pg->item()->id()));
        query.addQueryItem("colorID"_l1,  QString::number(pg->color()->id()));
        query.addQueryItem("cCode"_l1,    "USD"_l1);
        query.addQueryItem("cExc"_l1,     "Y"_l1); //  Y == exclude incomplete sets
    }
    url.setQuery(query);

    //qDebug ( "PG request started for %s", (const char *) url );
    pg->m_transferJob = TransferJob::get(url, nullptr, 2);
    pg->m_transferJob->setUserData("priceGuide", QVariant::fromValue(pg));

    m_transfer->retrieve(pg->m_transferJob, highPriority);
}

void Core::cancelPriceGuideUpdate(PriceGuide *pg)
{
    if (pg->m_transferJob)
        m_transfer->abortJob(pg->m_transferJob);
}


void Core::priceGuideJobFinished(TransferJob *j, PriceGuide *pg)
{
    pg->m_transferJob = nullptr;
    pg->m_update_status = UpdateStatus::UpdateFailed;

    if (j->isCompleted()) {
        if (pg->m_scrapedHtml)
            pg->m_valid = pg->parseHtml(*j->data(), pg->m_data);
        else
            pg->m_valid = pg->parse(*j->data(), pg->m_data);

        if (pg->m_valid) {
            pg->m_fetched = QDateTime::currentDateTime();
            pg->saveToDisk(pg->m_fetched, pg->m_data);
            pg->m_update_status = UpdateStatus::Ok;
        }
    } else if (!j->isAborted()) {
        qWarning() << "PriceGuide download failed:" << j->errorString() << "(" << j->responseCode() << ")";
    }

    emit priceGuideUpdated(pg);
    pg->release();
}


class PictureLoaderJob : public QRunnable
{
public:
    explicit PictureLoaderJob(Picture *pic)
        : QRunnable()
        , m_pic(pic)
    {
        pic->m_update_status = UpdateStatus::Loading;
    }

    void run() override;

    Picture *picture() const
    {
        return m_pic;
    }

private:
    Q_DISABLE_COPY(PictureLoaderJob)

    Picture *m_pic;
};

void PictureLoaderJob::run()
{
    if (m_pic) {
        QDateTime fetched;
        QImage image;
        bool valid = m_pic->loadFromDisk(fetched, image);
        auto pic = m_pic;
        QMetaObject::invokeMethod(core(), [=]() {
            pic->m_valid = valid;
            pic->m_update_status = UpdateStatus::Ok;
            if (valid) {
                pic->m_fetched = fetched;
                pic->m_image = image;
            }
            core()->pictureLoaded(pic);
        }, Qt::QueuedConnection);
    }
}


QSize Core::standardPictureSize() const
{
    QSize s(80, 60);
    qreal f = core()->itemImageScaleFactor();
    if (!qFuzzyCompare(f, 1))
        s *= f;
    return s;
}

Picture *Core::picture(const Item *item, const Color *color, bool highPriority)
{
    if (!item)
        return nullptr;

    quint64 key = Picture::key(item, color);
    Picture *pic = m_pic_cache[key];

    bool needToLoad = false;

    if (!pic) {
        pic = new Picture(item, color);
        if (!m_pic_cache.insert(key, pic, pic->cost())) {
            qWarning("Can not add picture to cache (cache max/cur: %d/%d, item: %s)",
                     int(m_pic_cache.maxCost()), int(m_pic_cache.totalCost()), item->id().constData());
            return nullptr;
        }
        needToLoad = true;
    }

    if (highPriority) {
        if (!pic->isValid()) {
            pic->m_valid = pic->loadFromDisk(pic->m_fetched, pic->m_image);
            pic->m_update_status = UpdateStatus::Ok;

            m_pic_cache.setObjectCost(key, pic->cost());
        }

        if (updateNeeded(pic->isValid(), pic->lastUpdated(), m_pic_update_iv))
            updatePicture(pic, highPriority);

    } else if (needToLoad) {
        pic->addRef();
        m_diskloadPool.start(new PictureLoaderJob(pic));
    }

    return pic;
}

Picture *Core::largePicture(const Item *item, bool high_priority)
{
    if (!item)
        return nullptr;
    return picture(item, nullptr, high_priority);
}


void Core::pictureLoaded(Picture *pic)
{
    if (pic) {
        if (pic->m_updateAfterLoad
                || updateNeeded(pic->isValid(), pic->lastUpdated(), m_pic_update_iv)) {
            pic->m_updateAfterLoad = false;
            updatePicture(pic, false);
        }
        emit pictureUpdated(pic);
        pic->release();

        m_pic_cache.setObjectCost(Picture::key(pic->item(), pic->color()), pic->cost());
    }
}

QPair<int, int> Core::pictureCacheStats() const
{
    return qMakePair(m_pic_cache.totalCost(), m_pic_cache.maxCost());
}

QPair<int, int> Core::priceGuideCacheStats() const
{
    return qMakePair(m_pg_cache.totalCost(), m_pg_cache.maxCost());
}

void Core::updatePicture(Picture *pic, bool highPriority)
{
    if (!pic || (pic->m_update_status == UpdateStatus::Updating))
        return;

    if (!m_online || !m_transfer) {
        pic->m_update_status = UpdateStatus::UpdateFailed;
        emit pictureUpdated(pic);
        return;
    }

    if (pic->m_update_status == UpdateStatus::Loading) {
        pic->m_updateAfterLoad = true;
        return;
    }

    pic->m_update_status = UpdateStatus::Updating;
    pic->addRef();

    bool large = (!pic->color());

    QString url;

    if (large) {
        url = u"https://img.bricklink.com/" % QLatin1Char(pic->item()->itemType()->pictureId())
                % u"L/" % QLatin1String(pic->item()->id()) % u".jpg";
    }
    else {
        url = u"https://img.bricklink.com/ItemImage/" % QLatin1Char(pic->item()->itemType()->pictureId())
                % u"N/" % QString::number(pic->color()->id()) % u'/'
                % QLatin1String(pic->item()->id()) % u".png";
    }

    //qDebug() << "PIC request started for" << url;
    QSaveFile *f = pic->saveFile();
    pic->m_transferJob = TransferJob::get(url, f);
    pic->m_transferJob->setUserData("picture", QVariant::fromValue(pic));
    m_transfer->retrieve(pic->m_transferJob, highPriority);
}

void Core::cancelPictureUpdate(Picture *pic)
{
    if (pic->m_transferJob)
        m_transfer->abortJob(pic->m_transferJob);
}


void Core::pictureJobFinished(TransferJob *j, Picture *pic)
{
    pic->m_transferJob = nullptr;
    bool large = (!pic->color());

    if (j->isCompleted() && j->file()) {
        static_cast<QSaveFile *>(j->file())->commit();

        // the pic is still ref'ed, so we just forward it to the loader
        pic->m_update_status = UpdateStatus::Loading;
        m_diskloadPool.start(new PictureLoaderJob(pic));
        return;

    } else if (large && (j->responseCode() == 404) && (j->url().path().endsWith(".jpg"_l1))) {
        // There's no large JPG image, so try a GIF image instead (mostly very old sets)
        // We save the GIF with an JPG extension if we succeed, but Qt uses the file header on
        // loading to do the right thing.

        if (!m_transfer) {
            pic->m_update_status = UpdateStatus::UpdateFailed;
        } else {
            pic->m_update_status = UpdateStatus::Updating;

            QUrl url = j->url();
            url.setPath(url.path().replace(".jpg"_l1, ".gif"_l1));

            QSaveFile *f = pic->saveFile();
            TransferJob *job = TransferJob::get(url, f);
            job->setUserData("picture", QVariant::fromValue<Picture *>(pic));
            m_transfer->retrieve(job);
            pic->m_transferJob = job;

            // the pic is still ref'ed: leave it that way for one more loop
            return;
        }
    } else {
        pic->m_update_status = UpdateStatus::UpdateFailed;

        qWarning() << "Image download failed:" << j->errorString() << "(" << j->responseCode() << ")";
    }

    emit pictureUpdated(pic);
    pic->release();
}

} // namespace BrickLink

#include "moc_core.cpp"

