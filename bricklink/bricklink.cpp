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
#include <cstdio>
#include <cstdlib>

#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QtXml/QDomDocument>
#include <QRegExp>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QDebug>
#include <QThread>

#include "cconfig.h"
#include "utility.h"
#include "stopwatch.h"
#include "bricklink.h"







/*
32 ID
32 VERSION
64 SIZE

SIZE BYTES DATA

PAD WITH 0 TO SIZE %16

64 SIZE
32 VERSION
32 ID
*/


#include <QStack>

#define ChunkId(a,b,c,d)  quint32((quint32(d & 0x7f) << 24) | (quint32(c & 0x7f) << 16) | (quint32(b & 0x7f) << 8) | quint32(a & 0x7f))

class ChunkReader {
public:
    ChunkReader(QIODevice *dev, QDataStream::ByteOrder bo);
    ~ChunkReader();

    QDataStream &dataStream();

    bool startChunk();
    bool endChunk();
    bool skipChunk();

    quint32 chunkId() const;
    quint32 chunkVersion() const;
    quint64 chunkSize() const;

private:
    struct chunk_info {
        quint32 id;
        quint32 version;
        quint64 startpos;
        quint64 size;
    };

    QStack<chunk_info> m_chunks;
    QIODevice * m_file;
    QDataStream m_stream;
};

ChunkReader::ChunkReader(QIODevice *dev, QDataStream::ByteOrder bo)
    : m_file(0), m_stream(0)
{
    if (dev->isSequential()) {
        qWarning("ChunkReader: device does not support direct access!");
        return;
    }

    if (!dev->isOpen() || !dev->isReadable()) {
        qWarning("ChunkReader: device is not open and readable!");
        return;
    }
    m_file = dev;
    m_stream.setDevice(dev);
    m_stream.setByteOrder(bo);
}

ChunkReader::~ChunkReader()
{
}

QDataStream &ChunkReader::dataStream()
{
    static QDataStream nullstream;

    if (!m_file)
        return nullstream;
    return m_stream;
}

bool ChunkReader::startChunk()
{
    if (!m_file)
        return false;

    if (!m_chunks.isEmpty()) {
        chunk_info parent = m_chunks.top();

        if (m_file->pos() >= qint64(parent.startpos + parent.size))
            return false;
    }

    chunk_info ci;

    m_stream >> ci.id >> ci.version >> ci.size;
    if (m_stream.status() == QDataStream::Ok) {
        ci.startpos = m_file->pos();
        m_chunks.push(ci);
        return true;
    }
    return false;
}

bool ChunkReader::skipChunk()
{
    if (!m_file || m_chunks.isEmpty())
        return false;

    chunk_info ci = m_chunks.top();

    m_stream.skipRawData(ci.size);
    return endChunk();
}

bool ChunkReader::endChunk()
{
    if (!m_file || m_chunks.isEmpty())
        return false;

    chunk_info ci = m_chunks.pop();

    quint64 endpos = m_file->pos();
    if (ci.startpos + ci.size != endpos) {
        qWarning("ChunkReader: called endChunk() on a position %d bytes from the chunk end.", ci.startpos + ci.size - endpos);
        m_file->seek(ci.startpos + ci.size);
    }

    quint64 padsize = ci.size % 16;
    if (padsize)
        m_stream.skipRawData(padsize);

    chunk_info ciend;
    m_stream >> ciend.size >> ciend.version >> ciend.id;
    return (m_stream.status() == QDataStream::Ok) && !memcmp(&ci, &ciend, sizeof(ci));
}

quint32 ChunkReader::chunkId() const
{
    if (m_chunks.isEmpty())
        return 0;
    return m_chunks.top().id;
}

quint32 ChunkReader::chunkVersion() const
{
    if (m_chunks.isEmpty())
        return 0;
    return m_chunks.top().version;
}

quint64 ChunkReader::chunkSize() const
{
    if (m_chunks.isEmpty())
        return 0;
    return m_chunks.top().size;
}


/*
    QFile f(...);
    if (f.open(QIODevice::WriteOnly)) {
        ChunkWriter cw(&f);
        QDataStream &ds;

        ds = cw.startChunk('BSDB'L, 1);
        ds << QString("INFO TEXT");
        ds = cw.startChunk('COLO', 1);
        cw.endChunk();
        cw.endChunk();
        f.close();
    }
  */

class ChunkWriter {
public:
    ChunkWriter(QIODevice *dev, QDataStream::ByteOrder bo);
    ~ChunkWriter();

    QDataStream &dataStream();

    bool startChunk(quint32 id, quint32 version = 0);
    bool endChunk();

private:
    struct chunk_info {
        quint32 id;
        quint32 version;
        quint64 startpos;
    };

    QStack<chunk_info> m_chunks;
    QIODevice * m_file;
    QDataStream m_stream;
};

ChunkWriter::ChunkWriter(QIODevice *dev, QDataStream::ByteOrder bo)
    : m_file(0), m_stream(0)
{
    if (dev->isSequential()) {
        qWarning("ChunkWriter: device does not support direct access!");
        return;
    }

    if (!dev->isOpen() || !dev->isWritable()) {
        qWarning("ChunkWriter: device is not yet open!");
        return;
    }
    m_file = dev;
    m_stream.setDevice(dev);
    m_stream.setByteOrder(bo);
}

ChunkWriter::~ChunkWriter()
{
    while (!m_chunks.isEmpty()) {
        if (m_file && m_file->isOpen())
            endChunk();
        else
            qWarning("ChunkWriter: file was closed before ending chunk %lc", m_chunks.pop().id);
    }
}

QDataStream &ChunkWriter::dataStream()
{
    static QDataStream nullstream;

    if (!m_file)
        return nullstream;
    return m_stream;
}

bool ChunkWriter::startChunk(quint32 id, quint32 version)
{
    m_stream << id << version << quint64(0);

    chunk_info ci;
    ci.id = id;
    ci.version = version;
    ci.startpos = m_file->pos();
    m_chunks.push(ci);

    return (m_stream.status() == QDataStream::Ok);
}

bool ChunkWriter::endChunk()
{
    if (!m_file || m_chunks.isEmpty())
        return false;

    chunk_info ci = m_chunks.pop();
    quint64 endpos = m_file->pos();
    m_file->seek(ci.startpos - sizeof(quint64));
    m_stream << quint64(endpos - ci.startpos);
    m_file->seek(endpos);

    static const char padbytes[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    quint64 padsize = (endpos - ci.startpos) % 16;
    if (padsize)
        m_stream.writeRawData(padbytes, padsize);

    m_stream << quint64(endpos - ci.startpos) << ci.version << ci.id;

    return (m_stream.status() == QDataStream::Ok);
}














QUrl BrickLink::Core::url(UrlList u, const void *opt, const void *opt2)
{
    QUrl url;

    switch (u) {
    case URL_InventoryRequest:
        url = "http://www.bricklink.com/bbsiXML.asp";
        break;

    case URL_WantedListUpload:
        url = "http://www.bricklink.com/wantedXML.asp";
        break;

    case URL_InventoryUpload:
        url = "http://www.bricklink.com/invXML.asp";
        break;

    case URL_InventoryUpdate:
        url = "http://www.bricklink.com/invXML.asp#update";
        break;

    case URL_CatalogInfo:
        if (opt) {
            const Item *item = static_cast <const Item *>(opt);

            url = "http://www.bricklink.com/catalogItem.asp";
            url.addQueryItem(QChar(item->itemType()->id()), item->id());
        }
        break;

    case URL_PriceGuideInfo:
        if (opt && opt2) {
            const Item *item = static_cast <const Item *>(opt);

            url = "http://www.bricklink.com/catalogPriceGuide.asp";
            url.addQueryItem(QChar(item->itemType()->id()), item->id());

            if (item->itemType()->hasColors())
                url.addQueryItem("colorID", QString::number(static_cast <const Color *>(opt2)->id()));
        }
        break;

    case URL_LotsForSale:
        if (opt && opt2) {
            const Item *item = static_cast <const Item *>(opt);

            url = "http://www.bricklink.com/search.asp";
            url.addQueryItem("viewFrom", "sa");
            url.addQueryItem("itemType", QChar(item->itemType()->id()));

            // workaround for BL not accepting the -X suffix for sets, instructions and boxes
            QString id = item->id();
            char itt = item->itemType()->id();

            if (itt == 'S' || itt == 'I' || itt == 'O') {
                int pos = id.lastIndexOf('-');
                if (pos >= 0)
                    id.truncate(pos);
            }
            url.addQueryItem("q", id);

            if (item->itemType()->hasColors())
                url.addQueryItem("colorID", QString::number(static_cast <const Color *>(opt2)->id()));
        }
        break;

    case URL_AppearsInSets:
        if (opt && opt2) {
            const Item *item = static_cast <const Item *>(opt);

            url = "http://www.bricklink.com/catalogItemIn.asp";
            url.addQueryItem(QChar(item->itemType()->id()), item->id());
            url.addQueryItem("in", "S");

            if (item->itemType()->hasColors())
                url.addQueryItem("colorID", QString::number(static_cast <const Color *>(opt2)->id()));
        }
        break;

    case URL_ColorChangeLog:
        url = "http://www.bricklink.com/catalogReqList.asp?pg=1&chgUserID=&viewActionType=R";
        break;

    case URL_ItemChangeLog:
        url = "http://www.bricklink.com/catalogReqList.asp?pg=1&chgUserID=&viewActionType=I";

        if (opt)
            url.addQueryItem("q", static_cast <const char *>(opt));
        break;

    case URL_PeeronInfo:
        if (opt) {
            url = "http://peeron.com/cgi-bin/invcgis/psearch";
            url.addQueryItem("query", static_cast <const Item *>(opt)->id());
            url.addQueryItem("limit", "none");
        }
        break;

    case URL_StoreItemDetail:
        if (opt) {
            url = "http://www.bricklink.com/inventory_detail.asp";
            url.addQueryItem("itemID", QString::number(*static_cast <const unsigned int *>(opt)));
        }
        break;

    default:
        break;
    }
    return url;
}


const QPixmap *BrickLink::Core::noImage(const QSize &s) const
{
    QString key = QString("%1x%2").arg(s.width()).arg(s.height());

    QMutexLocker lock(&m_corelock);

    QPixmap *pix = m_noimages.value(key);

    if (!pix) {
        pix = new QPixmap(s);
        QPainter p(pix);

        int w = pix->width();
        int h = pix->height();
        bool high = (w < h);

        p.fillRect(0, 0, w, h, Qt::white);

        QRect r(high ? 0 : (w - h) / 2, high ? (w -h) / 2 : 0, high ? w : h, high ? w : h);
        int w4 = r.width() / 4;
        r.adjust(w4, w4, -w4, -w4);

        QColor coltable [] = {
            QColor(0x00, 0x00, 0x00),
            QColor(0x3f, 0x3f, 0x3f),
            QColor(0xff, 0x7f, 0x7f)
        };

        for (int i = 0; i < 3; i++) {
            r.adjust(-1, -1, -1, -1);

            p.setPen(QPen(coltable [i], 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(r.x(), r.y(), r.right(), r.bottom());
            p.drawLine(r.right(), r.y(), r.x(), r.bottom());
        }
        p.end();

        m_noimages.insert(key, pix);
    }
    return pix;
}


const QPixmap *BrickLink::Core::colorImage(const Color *col, int w, int h) const
{
    if (!col || w <= 0 || h <= 0)
        return 0;

    QString key = QString("%1:%2x%3").arg(col->id()).arg(w).arg(h);

    QMutexLocker lock(&m_corelock);

    QPixmap *pix = m_colimages.value(key);

    if (!pix) {
        QColor c = col->color();

        pix = new QPixmap(w, h);
        QPainter p(pix);
        QRect r = pix->rect();

        QBrush brush;

        if (col->isGlitter()) {
            brush = QBrush(Utility::contrastColor(c, 0.25f), Qt::Dense6Pattern);
        }
        else if (col->isSpeckle()) {
            // hack for speckled colors
            QColor c2;

            if (!c.isValid()) {
                QByteArray name = col->name();
                int dash = name.indexOf('-');
                if (dash > 0) {
                    QByteArray basename = name.mid(8, dash - 8);
                    if (basename.startsWith("DB"))
                        basename.replace(0, 2, "Dark Bluish ");
                    QByteArray speckname = name.mid(dash + 1);

                    const BrickLink::Color *basec = colorFromName(basename.constData());
                    const BrickLink::Color *speckc = colorFromName(speckname.constData());

                    if (basec)
                        c = basec->color();
                    if (speckc)
                        c2 = speckc->color();
                }
            }
            if (c.isValid()) {
                if (!c2.isValid()) // fake
                    c2 = Utility::contrastColor(c, 0.20f);
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

        if (col->isTransparent() /*&& CResource::inst ( )->pixmapAlphaSupported ( )*/) {
            QLinearGradient gradient(0, 0, r.width(), r.height());
            gradient.setColorAt(0,   Qt::transparent);
            gradient.setColorAt(0.4, Qt::transparent);
            gradient.setColorAt(0.6, QColor(255, 255, 255, 100));
            gradient.setColorAt(1,   QColor(255, 255, 255, 100));

            p.fillRect(r, gradient);
	    }
        p.end();

        m_colimages.insert(key, pix);
    }
    return pix;
}



namespace {

static bool check_and_create_path(const QString &p)
{
    QFileInfo fi(p);

    if (!fi.exists()) {
        QDir::current().mkdir(p);
        fi.refresh();
    }
    return (fi.exists() && fi.isDir() && fi.isReadable() && fi.isWritable());
}

} // namespace

QString BrickLink::Core::dataPath() const
{
    return m_datadir;
}

QString BrickLink::Core::dataPath(const ItemType *item_type) const
{
    QString p = dataPath();
    p += item_type->id();
    p += '/';

    if (!check_and_create_path(p))
        return QString();

    return p;
}

QString BrickLink::Core::dataPath(const Item *item) const
{
    QString p = dataPath(item->itemType());
    p += item->m_id;
    p += '/';

    if (!check_and_create_path(p))
        return QString();

    return p;
}

QString BrickLink::Core::dataPath(const Item *item, const Color *color) const
{
    QString p = dataPath(item);
    p += QString::number(color->id());
    p += '/';

    if (!check_and_create_path(p))
        return QString();

    return p;
}



BrickLink::Core *BrickLink::Core::s_inst = 0;

BrickLink::Core *BrickLink::Core::create(const QString &datadir, QString *errstring)
{
    if (!s_inst) {
        s_inst = new Core(datadir);

        QString test = s_inst->dataPath();
        if (test.isNull() || !check_and_create_path(test)) {
            delete s_inst;
            s_inst = 0;

            if (errstring)
                *errstring = tr("Data directory \'%1\' is not both read- and writable.").arg(datadir);
        }
    }
    return s_inst;
}

BrickLink::Core::Core(const QString &datadir)
    : m_datadir(datadir), m_c_locale(QLocale::c()), m_corelock(QMutex::Recursive),
      m_color_model(0), m_category_model(0), m_itemtype_model (0), m_item_model(0),
      m_transfer(0), m_pg_update_iv(0), m_pic_update_iv(0),
      m_pic_diskload(QThread::idealThreadCount() * 3)
{
    if (m_datadir.isEmpty())
        m_datadir = "./";

    m_datadir = QDir::cleanPath(datadir);

    if (m_datadir.right(1) != "/")
        m_datadir += "/";

    m_online = true;

    // cache size is physical memory * 0.25, at least 64MB, at most 256MB
    quint64 cachemem = qBound(64ULL *1024*1024, Utility::physicalMemory() / 4, 256ULL *1024*1024);
    //quint64 cachemem = 1024*1024; // DEBUG

    QPixmapCache::setCacheLimit(20 * 1024);     // 80 x 60 x 32 (w x h x bpp) == 20kB ->room for ~1000 pixmaps
    m_pg_cache.setMaxCost(500);          // each priceguide has a cost of 1
    m_pic_cache.setMaxCost(cachemem);    // each pic has a cost of (w*h*d/8 + 1024)

    connect(&m_pic_diskload, SIGNAL(finished(CThreadPoolJob *)), this, SLOT(pictureLoaded(CThreadPoolJob *)));
}

BrickLink::Core::~Core()
{
    cancelPictureTransfers();
    cancelPriceGuideTransfers();

    qDeleteAll(m_noimages);

    s_inst = 0;
}

void BrickLink::Core::setTransfer(CTransfer *trans)
{
    CTransfer *old = m_transfer;

    m_transfer = trans;

    if (old) { // disconnect
        disconnect(old, SIGNAL(finished(CThreadPoolJob *)), this, SLOT(priceGuideJobFinished(CThreadPoolJob *)));
        disconnect(old, SIGNAL(finished(CThreadPoolJob *)), this, SLOT(pictureJobFinished(CThreadPoolJob *)));

        disconnect(old, SIGNAL(progress(int, int)), this, SIGNAL(pictureProgress(int, int)));
        disconnect(old, SIGNAL(progress(int, int)), this, SIGNAL(priceGuideProgress(int, int)));
    }
    if (trans) { // connect
        connect(trans, SIGNAL(finished(CThreadPoolJob *)), this, SLOT(priceGuideJobFinished(CThreadPoolJob *)));
        connect(trans, SIGNAL(finished(CThreadPoolJob *)), this, SLOT(pictureJobFinished(CThreadPoolJob *)));

        connect(trans, SIGNAL(progress(int, int)), this, SIGNAL(pictureProgress(int, int)));
        connect(trans, SIGNAL(progress(int, int)), this, SIGNAL(priceGuideProgress(int, int)));
    }
}

CTransfer *BrickLink::Core::transfer() const
{
    return m_transfer;
}

void BrickLink::Core::setUpdateIntervals(int pic, int pg)
{
    m_pic_update_iv = pic;
    m_pg_update_iv = pg;
}

bool BrickLink::Core::updateNeeded(const QDateTime &last, int iv)
{
    return (iv > 0) && (last.secsTo(QDateTime::currentDateTime()) > iv);
}

void BrickLink::Core::setOnlineStatus(bool on)
{
    m_online = on;
}

bool BrickLink::Core::onlineStatus() const
{
    return m_online;
}


const QHash<int, const BrickLink::Color *> &BrickLink::Core::colors() const
{
    return m_colors;
}

const QHash<int, const BrickLink::Category *> &BrickLink::Core::categories() const
{
    return m_categories;
}

const QHash<int, const BrickLink::ItemType *> &BrickLink::Core::itemTypes() const
{
    return m_item_types;
}

const QVector<const BrickLink::Item *> &BrickLink::Core::items() const
{
    return m_items;
}

const BrickLink::Category *BrickLink::Core::category(uint id) const
{
    return m_categories.value(id);
}

const BrickLink::Color *BrickLink::Core::color(uint id) const
{
    return m_colors.value(id);
}

const BrickLink::Color *BrickLink::Core::colorFromName(const char *name) const
{
    if (!name || !name[0])
        return 0;

    foreach(const Color *col, m_colors) {
        if (!qstricmp(col->name(), name))
            return col;
    }
    return 0;
}

const BrickLink::Color *BrickLink::Core::colorFromPeeronName(const char *peeron_name) const
{
    if (!peeron_name || !peeron_name[0])
        return 0;

    foreach(const Color *col, m_colors) {
        if (!qstricmp(col->peeronName(), peeron_name))
            return col;
    }
    return 0;
}


const BrickLink::Color *BrickLink::Core::colorFromLDrawId(int ldraw_id) const
{
    foreach(const Color *col, m_colors) {
        if (col->ldrawId() == ldraw_id)
            return col;
    }
    return 0;
}


const BrickLink::ItemType *BrickLink::Core::itemType(char id) const
{
    return m_item_types.value(id);
}

const BrickLink::Item *BrickLink::Core::item(char tid, const char *id) const
{
    Item key;
    key.m_item_type = itemType(tid);
    key.m_id = const_cast <char *>(id);

    Item *keyp = &key;

    Item **itp = (Item **) bsearch(&keyp, m_items.data(), m_items.count(), sizeof(Item *), (int (*)(const void *, const void *)) Item::compare);

    key.m_id = 0;
    key.m_item_type = 0;

    return itp ? *itp : 0;
}

void BrickLink::Core::cancelPictureTransfers()
{
    QMutexLocker lock(&m_corelock);

    m_pic_diskload.abortAllJobs();
    m_transfer->abortAllJobs();
}

void BrickLink::Core::cancelPriceGuideTransfers()
{
    QMutexLocker lock(&m_corelock);

    m_transfer->abortAllJobs();
}

QString BrickLink::Core::defaultDatabaseName(DatabaseVersion version) const
{
    return QString("database-v%1").arg(version);
}

bool BrickLink::Core::readDatabase(const QString &fname)
{
    QString filename = fname.isNull() ? dataPath() + defaultDatabaseName() : fname;

    QMutexLocker lock(&m_corelock);

    cancelPictureTransfers();
    cancelPriceGuideTransfers();

    m_pg_cache.clear();
    m_pic_cache.clear();
    QPixmapCache::clear();

    qDeleteAll(m_colors);
    qDeleteAll(m_item_types);
    qDeleteAll(m_categories);
    qDeleteAll(m_items);

    m_colors.clear();
    m_item_types.clear();
    m_categories.clear();
    m_items.clear();
    m_alltime_pg.clear();

    bool result = false;
    stopwatch *sw = 0; //new stopwatch("BrickLink::Core::readDatabase()");

    QFile f(filename);
    if (f.open(QFile::ReadOnly)) {
        const char *data = reinterpret_cast<char *>(f.map(0, f.size()));

        if (data) {
            QByteArray ba = QByteArray::fromRawData(data, f.size());
#if 1
            QBuffer buf(&ba);
            buf.open(QIODevice::ReadOnly);

            ChunkReader cr(&buf, QDataStream::LittleEndian);
            QDataStream &ds = cr.dataStream();

            if (cr.startChunk() && cr.chunkId() == ChunkId('B','S','D','B') && cr.chunkVersion() == 1) {
                while (result && cr.startChunk()) {
                    switch (cr.chunkId() | quint64(cr.chunkVersion()) << 32) {
                        case ChunkId('C','O','L',' ') | 1ULL << 32: {
                            quint32 colc = 0;
                            ds >> colc;

                            for (quint32 i = colc; i; i--) {
                                Color *col = new Color();
                                ds >> col;
                                m_colors.insert(col->id(), col);
                            }
                            break;
                        }
                        case ChunkId('C','A','T',' ') | 1ULL << 32: {
                            quint32 catc = 0;
                            ds >> catc;

                            for (quint32 i = catc; i; i--) {
                                Category *cat = new Category();
                                ds >> cat;
                                m_categories.insert(cat->id(), cat);
                            }
                            break;
                        }
                        case ChunkId('T','Y','P','E') | 1ULL << 32: {
                            quint32 ittc = 0;
                            ds >> ittc;

                            for (quint32 i = ittc; i; i--) {
                                ItemType *itt = new ItemType();
                                ds >> itt;
                                m_item_types.insert(itt->id(), itt);
                            }
                            break;
                        }
                        case ChunkId('I','T','E','M') | 1ULL << 32: {
                            quint32 itc = 0;
                            ds >> itc;

                            m_items.reserve(itc);
                            for (quint32 i = itc; i; i--) {
                                Item *item = new Item();
                                ds >> item;
                                m_items.append(item);
                            }
                            break;
                        }
                        case ChunkId('A','T','P','G') | 1ULL << 32: {
                            quint32 pgc = 0;
                            ds >> pgc;

                            m_alltime_pg.reserve(pgc);
                            ds.readRawData(m_alltime_pg.data(), pgc);
                            break;
                        }
                        default: {
                            if (!cr.skipChunk())
                                goto out;
                            break;
                        }
                    }
                    if (!cr.endChunk())
                        goto out;
                }
                if (!cr.endChunk())
                    goto out;
            }

#else
            QDataStream ds(ba);

            ds.setVersion(QDataStream::Qt_3_3);

            quint32 magic = 0, filesize = 0, version = 0;

            ds >> magic >> filesize >> version;

            if ((magic != quint32(0xb91c5703)) || (filesize != f.size()) || (version != DEFAULT_DATABASE_VERSION))
                return false;

            ds.setByteOrder(QDataStream::LittleEndian);

            // colors
            quint32 colc = 0;
            ds >> colc;

            for (quint32 i = colc; i; i--) {
                Color *col = new Color();
                ds >> col;
                m_colors.insert(col->id(), col);
            }

            // categories
            quint32 catc = 0;
            ds >> catc;

            for (quint32 i = catc; i; i--) {
                Category *cat = new Category();
                ds >> cat;
                m_categories.insert(cat->id(), cat);
            }

            // types
            quint32 ittc = 0;
            ds >> ittc;

            for (quint32 i = ittc; i; i--) {
                ItemType *itt = new ItemType();
                ds >> itt;
                m_item_types.insert(itt->id(), itt);
            }

            // items
            quint32 itc = 0;
            ds >> itc;

            m_items.reserve(itc);
            for (quint32 i = itc; i; i--) {
                Item *item = new Item();
                ds >> item;
                m_items.append(item);
            }
            quint32 allc = 0;
            ds >> allc >> magic;

            if ((allc == (colc + ittc + catc + itc)) && (magic == quint32(0xb91c5703))) {
                result = true;
            }
#endif
        }
    }

out:
    if (result) {
        delete sw;
#ifdef _MSC_VER
#define PF_SIZE_T   "I"
#else
#define PF_SIZE_T   "z"
#endif
        qDebug("Color: %8u  (%11" PF_SIZE_T "u bytes)", m_colors.count(),     m_colors.count()     * (sizeof(Color)    + 20));
        qDebug("Types: %8u  (%11" PF_SIZE_T "u bytes)", m_item_types.count(), m_item_types.count() * (sizeof(ItemType) + 20));
        qDebug("Cats : %8u  (%11" PF_SIZE_T "u bytes)", m_categories.count(), m_categories.count() * (sizeof(Category) + 20));
        qDebug("Items: %8u  (%11" PF_SIZE_T "u bytes)", m_items.count(),      m_items.count()      * (sizeof(Item)     + 20));
        qDebug("Price:           (%11u bytes)",         m_alltime_pg.size() + 20);
#undef PF_SIZE_T
    }
    else {
        m_colors.clear();
        m_item_types.clear();
        m_categories.clear();
        m_items.clear();
        m_alltime_pg.clear();

        qWarning("Error reading databases!");
    }
    return result;
}


BrickLink::InvItemList *BrickLink::Core::parseItemListXML(QDomElement root, ItemListXMLHint hint, uint *invalid_items)
{
    QString roottag, itemtag;

    switch (hint) {
    case XMLHint_BrikTrak  : roottag = "GRID"; itemtag = "ITEM"; break;
    case XMLHint_Order     : roottag = "ORDER"; itemtag = "ITEM"; break;
    case XMLHint_MassUpload:
    case XMLHint_MassUpdate:
    case XMLHint_WantedList:
    case XMLHint_Inventory : roottag = "INVENTORY"; itemtag = "ITEM"; break;
    case XMLHint_BrickStore: roottag = "Inventory"; itemtag = "Item"; break;
    }

    if (root.nodeName() != roottag)
        return 0;

    InvItemList *inv = new InvItemList();

    uint incomplete = 0;

    for (QDomNode itemn = root.firstChild(); !itemn.isNull(); itemn = itemn.nextSibling()) {
        if (itemn.nodeName() != itemtag)
            continue;

        InvItem *ii = new InvItem();

        QString itemid, itemname;
        QString itemtypeid, itemtypename;
        QString colorid, colorname;
        QString categoryid, categoryname;

        bool has_orig_price = false;
        bool has_orig_qty = false;

        for (QDomNode n = itemn.firstChild(); !n.isNull(); n = n.nextSibling()) {
            if (!n.isElement())
                continue;
            QString tag = n.toElement().tagName();
            QString val = n.toElement().text();

            // ### BrickLink XML & BrikTrak ###
            if (hint != XMLHint_BrickStore) {
                if (tag == (hint == XMLHint_BrikTrak ? "PART_NO" : "ITEMID"))
                    itemid = val;
                else if (tag == (hint == XMLHint_BrikTrak ? "COLOR_ID" : "COLOR"))
                    colorid = val;
                else if (tag == (hint == XMLHint_BrikTrak ? "CATEGORY_ID" : "CATEGORY"))
                    categoryid = val;
                else if (tag == (hint == XMLHint_BrikTrak ? "TYPE" : "ITEMTYPE"))
                    itemtypeid = val;
                else if (tag == "PRICE")
                    ii->setPrice(money_t::fromCString(val));
                else if (tag == "BULK")
                    ii->setBulkQuantity(val.toInt());
                else if (tag == "QTY")
                    ii->setQuantity(val.toInt());
                else if (tag == "SALE")
                    ii->setSale(val.toInt());
                else if (tag == "CONDITION")
                    ii->setCondition(val == "N" ? New : Used);
                else if (tag == (hint == XMLHint_BrikTrak ? "NOTES" : "DESCRIPTION"))
                    ii->setComments(val);
                else if (tag == "REMARKS")
                    ii->setRemarks(val);
                else if (tag == "TQ1")
                    ii->setTierQuantity(0, val.toInt());
                else if (tag == "TQ2")
                    ii->setTierQuantity(1, val.toInt());
                else if (tag == "TQ3")
                    ii->setTierQuantity(2, val.toInt());
                else if (tag == "TP1")
                    ii->setTierPrice(0, money_t::fromCString(val));
                else if (tag == "TP2")
                    ii->setTierPrice(1, money_t::fromCString(val));
                else if (tag == "TP3")
                    ii->setTierPrice(2, money_t::fromCString(val));
                else if (tag == "LOTID")
                    ii->setLotId(val.toUInt());
            }

            // ### BrickLink Order (workaround for broken BL script) ###
            if (hint == XMLHint_Order) {
                // The remove(',') stuff is a workaround for the
                // broken Order XML generator: the XML contains , as
                // thousands-separator: 1,752 instead of 1752

                if (tag == "QTY")
                    ii->setQuantity(val.remove(',').toInt());
            }

            // ### BrikTrak import ###
            if (hint == XMLHint_BrikTrak) {
                if (tag == "PART_DESCRIPTION")
                    itemname = val;
                else if (tag == "COLOR")
                    colorname = val;
                else if (tag == "CATEGORY")
                    categoryname = val;
                else if (tag == "CHECKBOX") {
                    switch (val.toInt()) {
                    case 0: ii->setStatus(Exclude); break;
                    case 1: ii->setStatus(Include); break;
                    case 3: ii->setStatus(Extra); break;
                    case 5: ii->setStatus(Unknown); break;
                    }
                }

                // the following tags are BrickStore extensions
                else if (tag == "RETAIN")
                    ii->setRetain(val == "Y");
                else if (tag == "STOCKROOM")
                    ii->setStockroom(val == "Y");
                else if (tag == "BUYERUSERNAME")
                    ii->setReserved(val);
            }

            // ### Inventory Request ###
            else if (hint == XMLHint_Inventory) {
                if ((tag == "EXTRA") && (val == "Y"))
                    ii->setStatus(Extra);
                else if (tag == "COUNTERPART")
                     ii->setCounterPart((val == "Y"));
                else if (tag == "ALTERNATE")
                     ii->setAlternate((val == "Y"));
                else if (tag == "MATCHID")
                     ii->setAlternateId(val.toInt());
                else if (tag == "ITEMNAME")    // BrickStore extension for Peeron inventories
                    itemname = val;
                else if (tag == "COLORNAME")   // BrickStore extension for Peeron inventories
                    colorname = val;
            }

            // ### BrickStore BSX ###
            else if (hint == XMLHint_BrickStore) {
                if (tag == "ItemID")
                    itemid = val;
                else if (tag == "ColorID")
                    colorid = val;
                else if (tag == "CategoryID")
                    categoryid = val;
                else if (tag == "ItemTypeID")
                    itemtypeid = val;
                else if (tag == "ItemName")
                    itemname = val;
                else if (tag == "ColorName")
                    colorname = val;
                else if (tag == "CategoryName")
                    categoryname = val;
                else if (tag == "ItemTypeName")
                    itemtypename = val;
                else if (tag == "Price")
                    ii->setPrice(money_t::fromCString(val));
                else if (tag == "Bulk")
                    ii->setBulkQuantity(val.toInt());
                else if (tag == "Qty")
                    ii->setQuantity(val.toInt());
                else if (tag == "Sale")
                    ii->setSale(val.toInt());
                else if (tag == "Condition")
                    ii->setCondition(val == "N" ? New : Used);
                else if (tag == "Comments")
                    ii->setComments(val);
                else if (tag == "Remarks")
                    ii->setRemarks(val);
                else if (tag == "TQ1")
                    ii->setTierQuantity(0, val.toInt());
                else if (tag == "TQ2")
                    ii->setTierQuantity(1, val.toInt());
                else if (tag == "TQ3")
                    ii->setTierQuantity(2, val.toInt());
                else if (tag == "TP1")
                    ii->setTierPrice(0, money_t::fromCString(val));
                else if (tag == "TP2")
                    ii->setTierPrice(1, money_t::fromCString(val));
                else if (tag == "TP3")
                    ii->setTierPrice(2, money_t::fromCString(val));
                else if (tag == "Status") {
                    Status st = Include;

                    if (val == "X")
                        st = Exclude;
                    else if (val == "I")
                        st = Include;
                    else if (val == "E")
                        st = Extra;
                    else if (val == "?")
                        st = Unknown;

                    ii->setStatus(st);
                }
                else if (tag == "LotID")
                    ii->setLotId(val.toUInt());
                else if (tag == "Retain")
                    ii->setRetain(true);
                else if (tag == "Stockroom")
                    ii->setStockroom(true);
                else if (tag == "Reserved")
                    ii->setReserved(val);
                else if (tag == "TotalWeight")
                    ii->setWeight(cLocale().toDouble(val));
                else if (tag == "OrigPrice") {
                    ii->setOrigPrice(money_t::fromCString(val));
                    has_orig_price = true;
                }
                else if (tag == "OrigQty") {
                    ii->setOrigQuantity(val.toInt());
                    has_orig_qty = true;
                }
            }
        }

        bool ok = true;

        //qDebug ( "item (id=%s, color=%s, type=%s, cat=%s", itemid.latin1 ( ), colorid.latin1 ( ), itemtypeid.latin1 ( ), categoryid.latin1 ( ));

        if (!has_orig_price)
            ii->setOrigPrice(ii->price());
        if (!has_orig_qty)
            ii->setOrigQuantity(ii->quantity());

        if (!colorid.isEmpty() && !itemid.isEmpty() && !itemtypeid.isEmpty()) {
            ii->setItem(item(itemtypeid [0].toLatin1(), itemid.toLatin1()));

            if (!ii->item()) {
                qWarning() << "failed: invalid itemid (" << itemid << ") and/or itemtypeid (" << itemtypeid [0] << ")";
                ok = false;
            }

            ii->setColor(color(colorid.toInt()));

            if (!ii->color()) {
                qWarning() << "failed: invalid color (" << colorid << ")";
                ok = false;
            }
        }
        else
            ok = false;

        if (!ok) {
            qWarning() << "failed: insufficient data (item=" << itemid << ", itemtype=" << itemtypeid [0] << ", category=" << categoryid << ", color=" << colorid << ")";

            InvItem::Incomplete *inc = new InvItem::Incomplete;

            if (!ii->item()) {
                inc->m_item_id = itemid;
                inc->m_item_name = itemname;
            }
            if (!ii->itemType()) {
                inc->m_itemtype_id = itemtypeid;
                inc->m_itemtype_name = itemtypename;
            }
            if (!ii->category()) {
                inc->m_category_id = categoryid;
                inc->m_category_name = categoryname;
            }
            if (!ii->color()) {
                inc->m_color_id = colorid;
                inc->m_color_name = colorname;
            }
            ii->setIncomplete(inc);

            ok = true;
            incomplete++;
        }

        if (ok)
            inv->append(ii);
        else
            delete ii;
    }

    if (invalid_items)
        *invalid_items = incomplete;
    if (incomplete)
        qWarning() << "Parse XML (hint=" << int(hint) << "): " << incomplete << " items have incomplete records";

    return inv;
}



QDomElement BrickLink::Core::createItemListXML(QDomDocument doc, ItemListXMLHint hint, const InvItemList *items, QMap <QString, QString> *extra)
{
    QString roottag, itemtag;

    switch (hint) {
    case XMLHint_BrikTrak  : roottag = "GRID"; itemtag = "ITEM"; break;
    case XMLHint_MassUpload:
    case XMLHint_MassUpdate:
    case XMLHint_WantedList:
    case XMLHint_Inventory : roottag = "INVENTORY"; itemtag = "ITEM"; break;
    case XMLHint_BrickStore: roottag = "Inventory"; itemtag = "Item"; break;
    case XMLHint_Order     : break;
    }

    QDomElement root = doc.createElement(roottag);

    if (root.isNull() || roottag.isNull() || itemtag.isEmpty() || !items)
        return root;

    foreach(const InvItem *ii, *items) {
        if (ii->isIncomplete())
            continue;

        if ((ii->status() == Exclude) && (hint != XMLHint_BrickStore && hint != XMLHint_BrikTrak))
            continue;

        if (hint == XMLHint_MassUpdate) {
            bool diffs = ((ii->quantity() != ii->origQuantity()) || (ii->price() != ii->origPrice()));

            if (!ii->lotId() || !diffs)
                continue;
        }

        QDomElement item = doc.createElement(itemtag);
        root.appendChild(item);

        // ### MASS UPDATE ###
        if (hint == XMLHint_MassUpdate) {
            item.appendChild(doc.createElement("LOTID").appendChild(doc.createTextNode(QString::number(ii->lotId()))).parentNode());

            int qdiff = ii->quantity() - ii->origQuantity();
            money_t pdiff = ii->price() - ii->origPrice();

            if (pdiff != 0)
                item.appendChild(doc.createElement("PRICE").appendChild(doc.createTextNode(ii->price().toCString())).parentNode());
            if (qdiff && (ii->quantity() > 0))
                item.appendChild(doc.createElement("QTY").appendChild(doc.createTextNode((qdiff > 0 ? "+" : "") + QString::number(qdiff))).parentNode());
            else if (qdiff && (ii->quantity() <= 0))
                item.appendChild(doc.createElement("DELETE"));
        }

        // ### BRIK TRAK EXPORT ###
        else if (hint == XMLHint_BrikTrak) {
            item.appendChild(doc.createElement("PART_NO").appendChild(doc.createTextNode(QString(ii->item()->id()))).parentNode());
            item.appendChild(doc.createElement("COLOR_ID").appendChild(doc.createTextNode(QString::number(ii->color()->id()))).parentNode());
            item.appendChild(doc.createElement("CATEGORY_ID").appendChild(doc.createTextNode(QString::number(ii->category()->id()))).parentNode());
            item.appendChild(doc.createElement("TYPE").appendChild(doc.createTextNode(QChar(ii->itemType()->id()))).parentNode());

            item.appendChild(doc.createElement("CATEGORY").appendChild(doc.createTextNode(ii->category()->name())).parentNode());
            item.appendChild(doc.createElement("PART_DESCRIPTION").appendChild(doc.createTextNode(ii->item()->name())).parentNode());
            item.appendChild(doc.createElement("COLOR").appendChild(doc.createTextNode(ii->color()->name())).parentNode());
            item.appendChild(doc.createElement("TOTAL_VALUE").appendChild(doc.createTextNode((ii->price() * ii->quantity()).toCString())).parentNode());

            int cb = 1;
            switch (ii->status()) {
            case Exclude: cb = 0; break;
            case Include: cb = 1; break;
            case Extra  : cb = 3; break;
            case Unknown: cb = 5; break;
            }
            item.appendChild(doc.createElement("CHECKBOX").appendChild(doc.createTextNode(QString::number(cb))).parentNode());
            item.appendChild(doc.createElement("QTY").appendChild(doc.createTextNode(QString::number(ii->quantity()))).parentNode());
            item.appendChild(doc.createElement("PRICE").appendChild(doc.createTextNode(ii->price().toCString())).parentNode());
            item.appendChild(doc.createElement("CONDITION").appendChild(doc.createTextNode((ii->condition() == New) ? "N" : "U")).parentNode());

            if (ii->bulkQuantity() != 1)
                item.appendChild(doc.createElement("BULK").appendChild(doc.createTextNode(QString::number(ii->bulkQuantity()))).parentNode());
            if (ii->sale())
                item.appendChild(doc.createElement("SALE").appendChild(doc.createTextNode(QString::number(ii->sale()))).parentNode());
            if (!ii->comments().isEmpty())
                item.appendChild(doc.createElement("NOTES").appendChild(doc.createTextNode(ii->comments())).parentNode());
            if (!ii->remarks().isEmpty())
                item.appendChild(doc.createElement("REMARKS").appendChild(doc.createTextNode(ii->remarks())).parentNode());

            if (ii->tierQuantity(0)) {
                item.appendChild(doc.createElement("TQ1").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(0)))).parentNode());
                item.appendChild(doc.createElement("TP1").appendChild(doc.createTextNode(ii->tierPrice(0).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ2").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(1)))).parentNode());
                item.appendChild(doc.createElement("TP2").appendChild(doc.createTextNode(ii->tierPrice(1).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ3").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(2)))).parentNode());
                item.appendChild(doc.createElement("TP3").appendChild(doc.createTextNode(ii->tierPrice(2).toCString())).parentNode());
            }
        }

        // ### BrickStore BSX ###
        else if (hint == XMLHint_BrickStore) {
            item.appendChild(doc.createElement("ItemID").appendChild(doc.createTextNode(QString(ii->item()->id()))).parentNode());
            item.appendChild(doc.createElement("ItemTypeID").appendChild(doc.createTextNode(QChar(ii->itemType()->id()))).parentNode());
            item.appendChild(doc.createElement("ColorID").appendChild(doc.createTextNode(QString::number(ii->color()->id()))).parentNode());

            // this extra information is useful, if the e.g.the color- or item-id
            // are no longer available after a database update
            item.appendChild(doc.createElement("ItemName").appendChild(doc.createTextNode(ii->item()->name())).parentNode());
            item.appendChild(doc.createElement("ItemTypeName").appendChild(doc.createTextNode(ii->itemType()->name())).parentNode());
            item.appendChild(doc.createElement("ColorName").appendChild(doc.createTextNode(ii->color()->name())).parentNode());
            item.appendChild(doc.createElement("CategoryID").appendChild(doc.createTextNode(QString::number(ii->category()->id()))).parentNode());
            item.appendChild(doc.createElement("CategoryName").appendChild(doc.createTextNode(ii->category()->name())).parentNode());

            const char *st;
            switch (ii->status()) {
            case Unknown: st = "?"; break;
            case Extra  : st = "E"; break;
            case Exclude: st = "X"; break;
            case Include:
            default     : st = "I"; break;
            }
            item.appendChild(doc.createElement("Status").appendChild(doc.createTextNode(st)).parentNode());
            item.appendChild(doc.createElement("Qty").appendChild(doc.createTextNode(QString::number(ii->quantity()))).parentNode());
            item.appendChild(doc.createElement("Price").appendChild(doc.createTextNode(ii->price().toCString())).parentNode());
            item.appendChild(doc.createElement("Condition").appendChild(doc.createTextNode((ii->condition() == New) ? "N" : "U")).parentNode());

            if (ii->bulkQuantity() != 1)
                item.appendChild(doc.createElement("Bulk").appendChild(doc.createTextNode(QString::number(ii->bulkQuantity()))).parentNode());
            if (ii->sale())
                item.appendChild(doc.createElement("Sale").appendChild(doc.createTextNode(QString::number(ii->sale()))).parentNode());
            if (!ii->comments().isEmpty())
                item.appendChild(doc.createElement("Comments").appendChild(doc.createTextNode(ii->comments())).parentNode());
            if (!ii->remarks().isEmpty())
                item.appendChild(doc.createElement("Remarks").appendChild(doc.createTextNode(ii->remarks())).parentNode());
            if (ii->retain())
                item.appendChild(doc.createElement("Retain"));
            if (ii->stockroom())
                item.appendChild(doc.createElement("Stockroom"));
            if (!ii->reserved().isEmpty())
                item.appendChild(doc.createElement("Reserved").appendChild(doc.createTextNode(ii->reserved())).parentNode());
            if (ii->lotId())
                item.appendChild(doc.createElement("LotID").appendChild(doc.createTextNode(QString::number(ii->lotId()))).parentNode());

            if (ii->tierQuantity(0)) {
                item.appendChild(doc.createElement("TQ1").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(0)))).parentNode());
                item.appendChild(doc.createElement("TP1").appendChild(doc.createTextNode(ii->tierPrice(0).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ2").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(1)))).parentNode());
                item.appendChild(doc.createElement("TP2").appendChild(doc.createTextNode(ii->tierPrice(1).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ3").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(2)))).parentNode());
                item.appendChild(doc.createElement("TP3").appendChild(doc.createTextNode(ii->tierPrice(2).toCString())).parentNode());
            }

            if (ii->m_weight > 0)
                item.appendChild(doc.createElement("TotalWeight").appendChild(doc.createTextNode(cLocale().toString(ii->weight(), 'f', 4))).parentNode());
            if (ii->origPrice() != ii->price())
                item.appendChild(doc.createElement("OrigPrice").appendChild(doc.createTextNode(ii->origPrice().toCString())).parentNode());
            if (ii->origQuantity() != ii->quantity())
                item.appendChild(doc.createElement("OrigQty").appendChild(doc.createTextNode(QString::number(ii->origQuantity()))).parentNode());
        }

        // ### MASS UPLOAD ###
        else if (hint == XMLHint_MassUpload) {
            item.appendChild(doc.createElement("ITEMID").appendChild(doc.createTextNode(QString(ii->item()->id()))).parentNode());
            item.appendChild(doc.createElement("COLOR").appendChild(doc.createTextNode(QString::number(ii->color()->id()))).parentNode());
            item.appendChild(doc.createElement("CATEGORY").appendChild(doc.createTextNode(QString::number(ii->category()->id()))).parentNode());
            item.appendChild(doc.createElement("ITEMTYPE").appendChild(doc.createTextNode(QChar(ii->itemType()->id()))).parentNode());

            item.appendChild(doc.createElement("QTY").appendChild(doc.createTextNode(QString::number(ii->quantity()))).parentNode());
            item.appendChild(doc.createElement("PRICE").appendChild(doc.createTextNode(ii->price().toCString())).parentNode());
            item.appendChild(doc.createElement("CONDITION").appendChild(doc.createTextNode((ii->condition() == New) ? "N" : "U")).parentNode());

            if (ii->bulkQuantity() != 1)
                item.appendChild(doc.createElement("BULK").appendChild(doc.createTextNode(QString::number(ii->bulkQuantity()))).parentNode());
            if (ii->sale())
                item.appendChild(doc.createElement("SALE").appendChild(doc.createTextNode(QString::number(ii->sale()))).parentNode());
            if (!ii->comments().isEmpty())
                item.appendChild(doc.createElement("DESCRIPTION").appendChild(doc.createTextNode(ii->comments())).parentNode());
            if (!ii->remarks().isEmpty())
                item.appendChild(doc.createElement("REMARKS").appendChild(doc.createTextNode(ii->remarks())).parentNode());
            if (ii->retain())
                item.appendChild(doc.createElement("RETAIN").appendChild(doc.createTextNode("Y")).parentNode());
            if (ii->stockroom())
                item.appendChild(doc.createElement("STOCKROOM").appendChild(doc.createTextNode("Y")).parentNode());
            if (!ii->reserved().isEmpty())
                item.appendChild(doc.createElement("BUYERUSERNAME").appendChild(doc.createTextNode(ii->reserved())).parentNode());

            if (ii->tierQuantity(0)) {
                item.appendChild(doc.createElement("TQ1").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(0)))).parentNode());
                item.appendChild(doc.createElement("TP1").appendChild(doc.createTextNode(ii->tierPrice(0).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ2").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(1)))).parentNode());
                item.appendChild(doc.createElement("TP2").appendChild(doc.createTextNode(ii->tierPrice(1).toCString())).parentNode());
                item.appendChild(doc.createElement("TQ3").appendChild(doc.createTextNode(QString::number(ii->tierQuantity(2)))).parentNode());
                item.appendChild(doc.createElement("TP3").appendChild(doc.createTextNode(ii->tierPrice(2).toCString())).parentNode());
            }
        }

        // ### WANTED LIST ###
        else if (hint == XMLHint_WantedList) {
            item.appendChild(doc.createElement("ITEMID").appendChild(doc.createTextNode(QString(ii->item()->id()))).parentNode());
            item.appendChild(doc.createElement("ITEMTYPE").appendChild(doc.createTextNode(QChar(ii->itemType()->id()))).parentNode());
            item.appendChild(doc.createElement("COLOR").appendChild(doc.createTextNode(QString::number(ii->color()->id()))).parentNode());

            if (ii->quantity())
                item.appendChild(doc.createElement("MINQTY").appendChild(doc.createTextNode(QString::number(ii->quantity()))).parentNode());
            if (ii->price() != 0)
                item.appendChild(doc.createElement("MAXPRICE").appendChild(doc.createTextNode(ii->price().toCString())).parentNode());
            if (!ii->remarks().isEmpty())
                item.appendChild(doc.createElement("REMARKS").appendChild(doc.createTextNode(ii->remarks())).parentNode());
            if (ii->condition() == New)
                item.appendChild(doc.createElement("CONDITION").appendChild(doc.createTextNode("N")).parentNode());
        }

        // ### INVENTORY REQUEST ###
        else if (hint == XMLHint_Inventory) {
            item.appendChild(doc.createElement("ITEMID").appendChild(doc.createTextNode(QString(ii->item()->id()))).parentNode());
            item.appendChild(doc.createElement("ITEMTYPE").appendChild(doc.createTextNode(QChar(ii->itemType()->id()))).parentNode());
            item.appendChild(doc.createElement("COLOR").appendChild(doc.createTextNode(QString::number(ii->color()->id()))).parentNode());
            item.appendChild(doc.createElement("QTY").appendChild(doc.createTextNode(QString::number(ii->quantity()))).parentNode());

            if (ii->status() == Extra)
                item.appendChild(doc.createElement("EXTRA").appendChild(doc.createTextNode("Y")).parentNode());
        }

        // optional: additonal tags
        if (extra) {
            for (QMap <QString, QString>::iterator it = extra->begin(); it != extra->end(); ++it)
                item.appendChild(doc.createElement(it.key()).appendChild(doc.createTextNode(it.value())).parentNode());
        }
    }

    return root;
}



bool BrickLink::Core::parseLDrawModel(QFile &f, InvItemList &items, uint *invalid_items)
{
    QHash<QString, InvItem *> mergehash;

    return parseLDrawModelInternal(f, QString::null, items, invalid_items, mergehash);
}

bool BrickLink::Core::parseLDrawModelInternal(QFile &f, const QString &model_name, InvItemList &items, uint *invalid_items, QHash<QString, InvItem *> &mergehash)
{
    QStringList searchpath;
    QString ldrawdir = CConfig::inst()->lDrawDir();
    uint invalid = 0;
    int linecount = 0;

    bool is_mpd = false;
    int current_mpd_index = -1;
    QString current_mpd_model;
    bool is_mpd_model_found = false;


    searchpath.append(QFileInfo(f).dir().absolutePath());
    if (!ldrawdir.isEmpty()) {
        searchpath.append(ldrawdir + "/models");
    }

    if (f.isOpen()) {
        QTextStream in(&f);
        QString line;

        while (!(line = in.readLine()).isNull()) {
            linecount++;

            if (!line.isEmpty() && line [0] == '0') {
                QStringList sl = line.simplified().split(' ');

                if ((sl.count() >= 2) && (sl [1] == "FILE")) {
                    is_mpd = true;
                    current_mpd_model = sl [2].toLower();
                    current_mpd_index++;

                    if (is_mpd_model_found)
                        break;

                    if ((current_mpd_model == model_name.toLower()) || (model_name.isEmpty() && (current_mpd_index == 0)))
                        is_mpd_model_found = true;

                    if (f.isSequential())
                        return false; // we need to seek!
                }
            }
            else if (!line.isEmpty() && line [0] == '1') {
                if (is_mpd && !is_mpd_model_found)
                    continue;

                QStringList sl = line.simplified().split(' ');

                if (sl.count() >= 15) {
                    int colid = sl [1].toInt();
                    QString partname = sl [14].toLower();

                    QString partid = partname;
                    partid.truncate(partid.lastIndexOf('.'));

                    const Color *colp = colorFromLDrawId(colid);
                    const Item *itemp = item('P', partid.toLatin1());


                    if (!itemp) {
                        bool got_subfile = false;

                        if (is_mpd) {
                            uint sub_invalid_items = 0;

                            qint64 oldpos = f.pos();
                            f.seek(0);

                            got_subfile = parseLDrawModelInternal(f, partname, items, &sub_invalid_items, mergehash);

                            invalid += sub_invalid_items;
                            f.seek(oldpos);
                        }

                        if (!got_subfile) {
                            for (QStringList::iterator it = searchpath.begin(); it != searchpath.end(); ++it) {
                                QFile subf(*it + "/" + partname);

                                if (subf.open(QIODevice::ReadOnly)) {
                                    uint sub_invalid_items = 0;

                                    (void) parseLDrawModelInternal(subf, partname, items, &sub_invalid_items, mergehash);

                                    invalid += sub_invalid_items;
                                    got_subfile = true;
                                    break;
                                }
                            }
                        }
                        if (got_subfile)
                            continue;
                    }

                    QString key = QString("%1@%2").arg(partid).arg(colid);

                    InvItem *ii = mergehash.value(key);

                    if (ii) {
                        ii->m_quantity++;
                    }
                    else {
                        ii = new InvItem(colp, itemp);
                        ii->m_quantity = 1;

                        if (!colp || !itemp) {
                            InvItem::Incomplete *inc = new InvItem::Incomplete;

                            if (!itemp) {
                                inc->m_item_id = partid;
                                inc->m_item_name = QString("LDraw ID: %1").arg(partid);
                                inc->m_itemtype_id = 'P';
                            }
                            if (!colp) {
                                inc->m_color_id = -1;
                                inc->m_color_name = QString("LDraw #%1").arg(colid);
                            }
                            ii->setIncomplete(inc);
                            invalid++;
                        }

                        items.append(ii);
                        mergehash.insert(key, ii);
                    }
                }
            }
        }
    }

    if (invalid_items)
        *invalid_items = invalid;

    return is_mpd ? is_mpd_model_found : true;
}



/*
 * Support routines to rebuild the DB from txt files
 */
void BrickLink::Core::setDatabase_ConsistsOf(const QHash<const Item *, InvItemList> &hash)
{
    QMutexLocker lock(&m_corelock);

    for (QHash<const Item *, InvItemList>::const_iterator it = hash.begin(); it != hash.end(); ++it)
        it.key()->setConsistsOf(it.value());
}

void BrickLink::Core::setDatabase_AppearsIn(const QHash<const Item *, AppearsIn> &hash)
{
    QMutexLocker lock(&m_corelock);

    for (QHash<const Item *, AppearsIn>::const_iterator it = hash.begin(); it != hash.end(); ++it)
        it.key()->setAppearsIn(it.value());
}

void BrickLink::Core::setDatabase_Basics(const QHash<int, const Color *> &colors,
        const QHash<int, const Category *> &categories,
        const QHash<int, const ItemType *> &item_types,
        const QVector<const Item *> &items)
{
    QMutexLocker lock(&m_corelock);

    cancelPictureTransfers();
    cancelPriceGuideTransfers();

    m_pg_cache.clear();
    m_pic_cache.clear();
    QPixmapCache::clear();

    m_colors.clear();
    m_item_types.clear();
    m_categories.clear();
    m_items.clear();

    m_colors     = colors;
    m_item_types = item_types;
    m_categories = categories;
    m_items      = items;
}

uint _bytes_for_alltime_pg = 0;

void BrickLink::Core::setDatabase_AllTimePG(const QList<AllTimePriceGuide> &list)
{
    QMutexLocker lock(&m_corelock);

    alltimepg_record *pg = reinterpret_cast<alltimepg_record *>(new char[sizeof(alltimepg_record) + 6*sizeof(float)]);

    for (QList<AllTimePriceGuide>::const_iterator it = list.begin(); it != list.end(); ++it) {
        int index = 0;

        pg->m_item_index = it->item->index() + 1;
        pg->m_color_id = it->color->id();
        pg->m_new_qty = it->condition[New].quantity;
        if (pg->m_new_qty) {
            pg->m_prices[index++] = it->condition[New].minPrice.toDouble();
            if (pg->m_new_qty > 1) {
                pg->m_prices[index++] = it->condition[New].avgPrice.toDouble();
                pg->m_prices[index++] = it->condition[New].maxPrice.toDouble();
            }
        }
        pg->m_used_qty = it->condition[Used].quantity;
        if (pg->m_used_qty) {
            pg->m_prices[index++] = it->condition[Used].minPrice.toDouble();
            if (pg->m_used_qty > 1) {
                pg->m_prices[index++] = it->condition[Used].avgPrice.toDouble();
                pg->m_prices[index++] = it->condition[Used].maxPrice.toDouble();
            }
        }

        int olds = m_alltime_pg.size();
        int news = sizeof(alltimepg_record) + sizeof(float) * index;

        m_alltime_pg.resize(olds + news);
        memcpy(m_alltime_pg.data() + olds, pg, news);
    }
    m_alltime_pg += QVector<char>(sizeof(quint32), 0);

    _bytes_for_alltime_pg = m_alltime_pg.size();

    delete pg;
}

bool BrickLink::Core::writeDatabase(const QString &filename, DatabaseVersion version)
{
    if (filename.isEmpty() || (version != BrickStore_1_1 && version != BrickStore_2_0))
        return false;

    QMutexLocker lock(&m_corelock);

    QFile f(filename + ".new");
    if (f.open(QIODevice::WriteOnly)) {
        if (version == BrickStore_1_1) {
            QDataStream ds(&f);
            ds.setVersion(QDataStream::Qt_3_3);

            ds << quint32(0 /*magic*/) << quint32(0 /*filesize*/) << quint32(BrickStore_1_1 /*version*/);

            ds.setByteOrder(QDataStream::LittleEndian);

            // colors
            quint32 colc = m_colors.count();
            ds << colc;
            foreach(const Color *col, m_colors)
                ds << col;

            // categories
            quint32 catc = m_categories.count();
            ds << catc;
            foreach(const Category *cat, m_categories)
                ds << cat;

            // types
            quint32 ittc = m_item_types.count();
            ds << ittc;
            foreach(const ItemType *itt, m_item_types)
                ds << itt;

            // items
            quint32 itc = m_items.count();
            ds << itc;
            const Item **itp = m_items.data();
            for (quint32 i = itc; i; ++itp, --i)
                ds << *itp;

            ds << (colc + ittc + catc + itc);
            ds << quint32(0xb91c5703);

            quint32 filesize = quint32(f.pos());

            if (f.error() == QFile::NoError) {
                f.close();

                if (f.open(QIODevice::ReadWrite)) {
                    QDataStream ds2(&f);
                    ds2 << quint32(0xb91c5703) << filesize;

                    if (f.error() == QFile::NoError) {
                        f.close();

                        QString err = Utility::safeRename(filename);

                        if (err.isNull())
                            return true;
                    }
                }
            }
        }
        else if (version == BrickStore_2_0) {
            ChunkWriter cw(&f, QDataStream::LittleEndian);
            QDataStream &ds = cw.dataStream();
            bool ok = true;

            ok &= cw.startChunk(ChunkId('B','S','D','B'), version);
            ds << QString("INFO TEXT");

            ok &= cw.startChunk(ChunkId('C','O','L',' '), version);
            ds << m_colors.count();
            foreach(const Color *col, m_colors)
                ds << col;
            ok &= cw.endChunk();

            ok &= cw.startChunk(ChunkId('C','A','T',' '), version);
            ds << m_categories.count();
            foreach(const Category *cat, m_categories)
                ds << cat;
            ok &= cw.endChunk();

            ok &= cw.startChunk(ChunkId('T','Y','P','E'), version);
            ds << m_item_types.count();
            foreach(const ItemType *itt, m_item_types)
                ds << itt;
            ok &= cw.endChunk();

            ok &= cw.startChunk(ChunkId('I','T','E','M'), version);
            quint32 itc = m_items.count();
            ds << itc;
            const Item **itp = m_items.data();
            for (quint32 i = itc; i; ++itp, --i)
                ds << *itp;
            ok &= cw.endChunk();

            ok &= cw.startChunk(ChunkId('A','T','P','G'), version);
            ds << quint32(m_alltime_pg.size());
            ds.writeRawData(m_alltime_pg.constData(), m_alltime_pg.size());
            ok &= cw.endChunk();

            ok &= cw.endChunk();

            if (ok && f.error() == QFile::NoError) {
                f.close();

                QString err = Utility::safeRename(filename);

                if (err.isNull())
                    return true;
            }
        }
    }
    if (f.isOpen())
        f.close();

    QFile::remove(filename + ".new");
    return false;
}


