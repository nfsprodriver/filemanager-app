/**************************************************************************
 *
 * Copyright 2014 Canonical Ltd()->
 * Copyright 2014 Carlos J Mazieri <carlos.mazieri@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * File: diriteminfo.cpp
 * Date: 30/01/2014
 */

#include "diriteminfo.h"




#include <QUrl>
#include <QDir>


class  DirItemInfoPrivate : public QSharedData
{
public:
    DirItemInfoPrivate();
    DirItemInfoPrivate(const DirItemInfoPrivate& other);
    DirItemInfoPrivate(const QFileInfo& fi);
    void setFileInfo(const QFileInfo&);

public:
    bool      _isValid     :1;
    bool      _isLocal     :1;
    bool      _isRemote    :1;
    bool      _isSelected  :1;
    bool      _isAbsolute  :1;
    bool      _exists      :1;
    bool      _isFile      :1;
    bool      _isDir       :1;
    bool      _isSymLink   :1;
    bool      _isRoot      :1;
    bool      _isReadable  :1;
    bool      _isWritable  :1;
    bool      _isExecutable:1;
    QFile::Permissions  _permissions;
    qint64    _size;
    QDateTime _created;
    QDateTime _lastModified;
    QDateTime _lastRead;
    QString   _path;
    QString   _fileName;
#if QT_VERSION >= 0x050000
        static QMimeDatabase mimeDatabase;
#endif
};

#if QT_VERSION >= 0x050000
 QMimeDatabase DirItemInfoPrivate::mimeDatabase;
#endif

DirItemInfoPrivate::DirItemInfoPrivate() :
   _isValid(false)
  ,_isLocal(false)
  ,_isRemote(false)
  ,_isSelected(false)
  ,_isAbsolute(false)
  ,_exists(false)
  ,_isDir(false)
  ,_isSymLink(false)
  ,_isRoot(false)
  ,_isReadable(false)
  ,_isWritable(false)
  ,_isExecutable(false)
  ,_permissions(0)
  ,_size(0)
{

}


DirItemInfoPrivate::DirItemInfoPrivate(const DirItemInfoPrivate &other):
   QSharedData(other)
  ,_isValid(other._isValid)
  ,_isLocal(other._isLocal)
  ,_isRemote(other._isRemote)
  ,_isSelected(other._isSelected)
  ,_isAbsolute(other._isAbsolute)
  ,_exists(other._exists)
  ,_isDir(other._isDir)
  ,_isSymLink(other._isSymLink)
  ,_isRoot(other._isRoot)
  ,_isReadable(other._isReadable)
  ,_isWritable(other._isWritable)
  ,_isExecutable(other._isExecutable)
  ,_permissions(other._permissions)
  ,_size(other._size)
  ,_created(other._created)
  ,_lastModified(other._lastModified)
  ,_lastRead(other._lastRead)
  ,_path(other._path)
  ,_fileName(other._fileName)
{

}


DirItemInfoPrivate::DirItemInfoPrivate(const QFileInfo &fi):
   _isValid(true)
  ,_isLocal(true)
  ,_isRemote(false)
  ,_isSelected(false)
{
    setFileInfo(fi);
}

void DirItemInfoPrivate::setFileInfo(const QFileInfo &fi)
{
    if (fi.exists() && fi.isRelative())
    {
        QFileInfo abs(fi.absoluteFilePath());
        setFileInfo(abs);
    }
    else
    {
        _path          = fi.absolutePath();
        _fileName      = fi.fileName();
        _isAbsolute    = fi.isAbsolute();
        _exists        = fi.exists();
        _isDir         = fi.isDir();
        _isFile        = fi.isFile();
        _isSymLink     = fi.isSymLink();
        _isRoot        = fi.isRoot();
        _isReadable    = fi.isReadable();
        _isWritable    = fi.isWritable();
        _isExecutable  = fi.isExecutable();
        _permissions   = fi.permissions();
        _size          = fi.size();
        _created       = fi.created();
        _lastRead      = fi.lastRead();
        _lastModified  = fi.lastModified();
    }
}

//================================================================

DirItemInfo::DirItemInfo(): d_ptr(new DirItemInfoPrivate())
{
}


DirItemInfo::~DirItemInfo()
{
}



DirItemInfo::DirItemInfo(const QFileInfo &fi):
    d_ptr(new DirItemInfoPrivate(fi))
{

}



DirItemInfo::DirItemInfo(const QString& urlOrPath):
    d_ptr(  new DirItemInfoPrivate(QFileInfo(urlOrPath)) )
{

}


DirItemInfo::DirItemInfo(const DirItemInfo& other)
{
   d_ptr = other.d_ptr;
}


bool DirItemInfo::isSelected() const
{
    return d_ptr->_isSelected;
}

/*!
 * \brief DirItemInfo::setSelection()
 * \param selected true/false new selection state
 * \return true if a new state was set, false if the selection is already equal to \a selected
 */
bool DirItemInfo::setSelection(bool selected)
{
    bool ret = selected != isSelected();
    d_ptr->_isSelected = selected;
    return ret;
}


bool DirItemInfo::isValid() const
{
    return d_ptr->_isValid;
}

bool DirItemInfo::isLocal() const
{
    return d_ptr->_isLocal;
}

bool DirItemInfo::isRemote() const
{
    return d_ptr->_isRemote;
}

bool DirItemInfo::exists() const
{
    return d_ptr->_exists;
}

QString DirItemInfo::filePath() const
{
   QString filepath;
   if (!d_ptr->_path.isEmpty())
   {
       filepath = d_ptr->_path;
       if (d_ptr->_path != QDir::rootPath())
       {
           filepath += QDir::separator();
       }
   }
   filepath += d_ptr->_fileName;
   return filepath;
}

QString DirItemInfo::fileName() const
{
    return d_ptr->_fileName;
}

QString DirItemInfo::absoluteFilePath() const
{
    return filePath();
}

QString DirItemInfo::absolutePath() const
{
    return d_ptr->_path;
}

bool  DirItemInfo::isReadable() const
{
    return d_ptr->_isReadable;
}

bool DirItemInfo::isWritable() const
{
    return d_ptr->_isWritable;
}

bool DirItemInfo::isExecutable() const
{
    return d_ptr->_isExecutable;
}

bool DirItemInfo::isRelative() const
{
    return ! isAbsolute();
}

bool  DirItemInfo::isAbsolute() const
{
    return d_ptr->_isAbsolute;
}

bool  DirItemInfo::isFile() const
{
    return d_ptr->_isFile;
}

bool DirItemInfo::isDir() const
{
    return d_ptr->_isDir;
}

bool DirItemInfo::isSymLink() const
{
    return d_ptr->_isSymLink;
}

bool DirItemInfo::isRoot() const
{
    return d_ptr->_isRoot;
}

QFile::Permissions  DirItemInfo::permissions() const
{
    return d_ptr->_permissions;
}

qint64 DirItemInfo::size() const
{
    return d_ptr->_size;
}

QDateTime DirItemInfo::created() const
{
    return d_ptr->_created;
}

QDateTime DirItemInfo::lastModified() const
{
    return d_ptr->_lastModified;
}

QDateTime DirItemInfo::lastRead() const
{
    return d_ptr->_lastRead;
}

void DirItemInfo::setFile(const QString &dir, const QString &file)
{
   QFileInfo f;
   f.setFile(dir,file);
   d_ptr->setFileInfo(f);
}

QFileInfo DirItemInfo::diskFileInfo() const
{
    QFileInfo fi(absoluteFilePath());
    return fi;
}

QString DirItemInfo::path() const
{
    return d_ptr->_path;
}

#if QT_VERSION >= 0x050000
QMimeType DirItemInfo::mimeType() const
{
    return d_ptr->mimeDatabase.mimeTypeForFile(diskFileInfo());
}
#endif
