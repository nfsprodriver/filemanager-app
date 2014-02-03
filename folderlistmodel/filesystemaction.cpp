/**************************************************************************
 *
 * Copyright 2013 Canonical Ltd.
 * Copyright 2013 Carlos J Mazieri <carlos.mazieri@gmail.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 *
 * File: filesystemaction.cpp
 * Date: 3/13/2013
 */

#include "filesystemaction.h"
#include "clipboard.h"

#if defined(Q_OS_UNIX)
#include <sys/statvfs.h>
#endif

#include <errno.h>

#include <QDirIterator>
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QTemporaryFile>

/*!
 *   number of the files to work on a step, when this number is reached a signal is emitted
 */
#define  STEP_FILES               5

/*!
  * buffer size to to single read/write operation
 */
#define  COPY_BUFFER_SIZE         4096

/*!
 *  Auxiliar Actions do not emit progress() signal
 *  \sa moveDirToTempAndRemoveItLater()
 */
#define SHOULD_EMIT_PROGRESS_SIGNAL(action)       (!action->isAux)

#define   COMMON_SIZE_ITEM       120




void FileSystemAction::CopyFile::clear()
{
    bytesWritten = 0;
    if (source)   delete source;
    if (target)   delete target;
    source = 0;
    target = 0;
}



//===============================================================================================
/*!
 * \brief FileSystemAction::FileSystemAction
 * \param parent
 */
FileSystemAction::FileSystemAction(QObject *parent) :
    QObject(parent)
  , m_curAction(0)
  , m_cancelCurrentAction(false)
  , m_busy(false)  
  , m_clipboardChanged(false)
{

}

//===============================================================================================
/*!
 * \brief FileSystemAction::~FileSystemAction
 */
FileSystemAction::~FileSystemAction()
{   

}

//===============================================================================================
/*!
 * \brief FileSystemAction::remove
 * \param paths
 */
void FileSystemAction::remove(const QStringList &paths)
{
    createAndProcessAction(ActionRemove, paths);
}

//===============================================================================================
/*!
 * \brief FileSystemAction::createAction
 * \param type
 * \param origBase
 * \return
 */
FileSystemAction::Action* FileSystemAction::createAction(ActionType type, int  origBase)
{
    Action * action = new Action();
    action->type         = type;
    action->baseOrigSize = origBase;
    action->targetPath   = m_path;
    action->totalItems   = 0;
    action->currItem     = 0;
    action->currEntryIndex    = 0;
    action->totalBytes   = 0;
    action->bytesWritten = 0;
    action->done         = false;
    action->auxAction    = 0;
    action->isAux        = false;
    action->currEntry    = 0;
    action->steps        = 1;

    return action;
}

//===============================================================================================
/*!
 * \brief FileSystemAction::addEntry
 * \param action
 * \param pathname
 */
void  FileSystemAction::addEntry(Action* action, const QString& pathname)
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << pathname;
#endif
    QFileInfo info(pathname);
    if (!info.isAbsolute())
    {
        info.setFile(action->targetPath, pathname);
    }
    if (!info.exists())
    {
        emit error(QObject::tr("File or Directory does not exist"),
                   pathname + QObject::tr(" does not exist")
                  );       
        return;
    }
    ActionEntry * entry = new ActionEntry();
    //this is the item being handled
    entry->reversedOrder.append(info);
    // verify if the destination item already exists
    if (action->type == ActionCopy ||
        action->type == ActionMove ||
        action->type == ActionHardMoveCopy)
    {
        QFileInfo destination(targetFom(info.absoluteFilePath(), action));
        entry->alreadyExists = destination.exists();
    }
    //ActionMove will perform a rename, so no Directory expanding is necessary
    if (action->type != ActionMove && info.isDir() && !info.isSymLink())
    {
        QDirIterator it(info.absoluteFilePath(),
                        QDir::AllEntries | QDir::System |
                              QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext() &&  !it.next().isEmpty())
        {
            entry->reversedOrder.prepend(it.fileInfo());
        }
    }
    //set steps and total bytes considering all items in the Entry
    int counter = entry->reversedOrder.count();
    qint64 size = 0;
    int sizeSteps = 0;
    int bufferSize = (COPY_BUFFER_SIZE * STEP_FILES);
    while (counter--)
            {
        const QFileInfo & item =  entry->reversedOrder.at(counter);
        size =  (item.isFile() && !item.isDir() && !item.isSymLink()) ?
                 item.size() :   COMMON_SIZE_ITEM;
        action->totalBytes +=  size;
        if (action->type == ActionCopy || action->type == ActionHardMoveCopy)
        {
            if ( (sizeSteps = size / bufferSize) )
            {
                if ( !(size % bufferSize) )
                {
                    --sizeSteps;
            }
                action->steps      += sizeSteps ;
        }
    }
    }
    //set final steps for the Entry based on Items number
    int entrySteps = entry->reversedOrder.count() / STEP_FILES;
    if ( entry->reversedOrder.count() % STEP_FILES) entrySteps++;
    action->steps      += entrySteps;
    action->totalItems += entry->reversedOrder.count();
#if DEBUG_MESSAGES
    qDebug() << "entrySteps" << entrySteps << "from entry counter" << entry->reversedOrder.count()
             << "total steps" << action->steps;
#endif
    //now put the Entry in the Action
    action->entries.append(entry);
}

//===============================================================================================
/*!
 * \brief FileSystemAction::processAction
 */
void FileSystemAction::processAction()
{
    if (m_curAction)
    {
        //it will be ActionHardMoveRemove only when switched from ActionHardMoveCopy
        //in this case the move is done in two steps COPY and REMOVE
        if (m_curAction->type != ActionHardMoveCopy)
        {
            delete m_curAction;
            m_curAction = 0;
        }
    }
    if (!m_curAction && m_queuedActions.count())
    {
        m_curAction = m_queuedActions.at(0);
        m_curAction->currEntry = static_cast<ActionEntry*>
                ( m_curAction->entries.at(0));
        m_queuedActions.remove(0,1);
    }
    if (m_curAction)
    {
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << "performing action type" << m_curAction->type;
#endif
        m_busy = true;
        m_cancelCurrentAction = false;
        m_errorMsg.clear();
        m_errorTitle.clear();
        scheduleSlot(SLOT(processActionEntry()));
        if (SHOULD_EMIT_PROGRESS_SIGNAL(m_curAction))
        {
            emit progress(0,m_curAction->totalItems, 0);
        }
    }
    else
    {
         m_busy = false;
    }
}


//===============================================================================================
/*!
 * \brief FileSystemAction::processActionEntry
 */
void FileSystemAction::processActionEntry()
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO;
#endif

    ActionEntry * curEntry = m_curAction->currEntry;

#if defined(SIMULATE_LONG_ACTION)
    {
        unsigned int delay = SIMULATE_LONG_ACTION;
        if (delay == 1)
        {
            delay = 100;           //each (10 * STEP_FILES) files will waits a second
            QThread::currentThread()->wait(delay);
        }
    }
#endif
    if (!m_cancelCurrentAction)
    {
        switch(m_curAction->type)
        {
           case ActionRemove:
           case ActionHardMoveRemove:
                removeEntry(curEntry);
                endActionEntry();
                break;
           case ActionCopy:
           case ActionHardMoveCopy:
                processCopyEntry();          // specially: this is a slot
                break;
          case ActionMove:
                moveEntry(curEntry);
                endActionEntry();
                break;
        }
    }
}

//===============================================================================================
/*!
 * \brief FileSystemAction::endActionEntry
 */
void FileSystemAction::endActionEntry()
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO;
#endif
     ActionEntry * curEntry = m_curAction->currEntry;

    // first of all check for any error or a cancel issued by the user
    if (m_cancelCurrentAction)
    {
        if (!m_errorTitle.isEmpty())
        {
            emit error(m_errorTitle, m_errorMsg);
        }
        //it may have other actions to do
        scheduleSlot(SLOT(processAction()));
        return;
    }
    // check if the current entry has finished
    // if so Views need to receive the notification about that
    if (curEntry->currItem == curEntry->reversedOrder.count())
    {
        const QFileInfo & mainItem = curEntry->reversedOrder.at(curEntry->currItem -1);
        m_curAction->currEntryIndex++;
        switch(m_curAction->type)
        {
           case ActionRemove:           
                emit removed(mainItem);
                break;
           case ActionHardMoveRemove: // nothing to do
                break;
           case ActionHardMoveCopy:
                //check if is doing a hard move and the copy part has finished
                //if so switch the action to remove
                if (m_curAction->currEntryIndex == m_curAction->entries.count())
                {
                   m_curAction->type      = ActionHardMoveRemove;
                   m_curAction->currEntryIndex = 0;
                   int entryCounter = m_curAction->entries.count();
                   ActionEntry * entry;
                   while (entryCounter--)
                   {
                       entry = m_curAction->entries.at(entryCounter);
                       entry->currItem = 0;
                       entry->currStep = 0;
                   }
                }
           case ActionCopy: // ActionHardMoveCopy is also checked here
           case ActionMove:
                {
                    QString addedItem = targetFom(mainItem.absoluteFilePath(), m_curAction);
                    if (!curEntry->added && !curEntry->alreadyExists)
                    {
                        emit added(addedItem);
                        curEntry->added = true;
                    }
                    else
                    {
                        emit changed(QFileInfo(addedItem));
                    }
                }
                break;
        }//switch

    }//end if (curEntry->currItem == curEntry->reversedOrder.count())

    if (curEntry->currStep == STEP_FILES)
    {
        curEntry->currStep = 0;
    }

    int percent = notifyProgress();
    //Check if the current action has finished or cancelled
    if (m_cancelCurrentAction ||
        m_curAction->currEntryIndex == m_curAction->entries.count())
    {
        if (!m_cancelCurrentAction)
        {
            endCurrentAction();
            if (percent < 100)
            {
                notifyProgress(100);
            }
        }
        //it may have other actions to do
        scheduleSlot(SLOT(processAction()));
    }
    else
    {
        m_curAction->currEntry = static_cast<ActionEntry*>
                ( m_curAction->entries.at(m_curAction->currEntryIndex) );
        //keep working on current Action maybe more entries
        scheduleSlot(SLOT(processActionEntry()));
    }
}

//===============================================================================================
/*!
 * \brief FileSystemAction::cancel
 */
void FileSystemAction::cancel()
{
    m_cancelCurrentAction = true;
}

//===============================================================================================
/*!
 * \brief FileSystemAction::removeEntry
 * \param entry
 */
void FileSystemAction::removeEntry(ActionEntry *entry)
{
    QDir dir;
    //do one step at least
    for(; !m_cancelCurrentAction                          &&
          entry->currStep       < STEP_FILES              &&
          m_curAction->currItem < m_curAction->totalItems &&
          entry->currItem       < entry->reversedOrder.count()
        ; entry->currStep++,    m_curAction->currItem++, entry->currItem++
        )

    {
        const QFileInfo &fi = entry->reversedOrder.at(entry->currItem);
        if (fi.isDir() && !fi.isSymLink())
        {
            m_cancelCurrentAction = !dir.rmdir(fi.absoluteFilePath());
        }
        else
        {
            m_cancelCurrentAction = !QFile::remove(fi.absoluteFilePath());
        }
#if DEBUG_REMOVE
        qDebug() << Q_FUNC_INFO << "remove ret=" << !m_cancelCurrentAction << fi.absoluteFilePath();
#endif
        if (m_cancelCurrentAction)
        {
            m_errorTitle = QObject::tr("Could not remove the item ") +
                                       fi.absoluteFilePath();
            m_errorMsg   = ::strerror(errno);
        }
    }
}


//===============================================================================================
/*!
 * \brief FileSystemAction::copyEntry
 * \param entry
 */
void  FileSystemAction::processCopyEntry()
{
     ActionEntry * entry = m_curAction->currEntry;

#if DEBUG_MESSAGES
     qDebug() << Q_FUNC_INFO << "processing"
              << entry->reversedOrder.at(entry->reversedOrder.count() -1).absoluteFilePath();
#endif
    /*
     * This flag will be true when processCopySingleFile() has put any slot in the execution queue
     * it will work to stop the loop.
     * Later processCopyEntry() will be called again to continue working
     */
    bool scheduleAnySlot = false;

    //first item from an Entry,    
    if (entry->currItem == 0 && entry->alreadyExists && entry->newName == 0)
    {
        //making backup only if the targetpath == origPath, otherwise the item is overwritten
        if (m_curAction->targetPath == m_curAction->origPath)
        {
            //it will check again if the target exists
            //if so, sets the entry->newName
            //then targetFom() will use entry->newName for
            //  sub items in the Entry if the Entry is a directory
            if (!makeBackupNameForCurrentItem(m_curAction) )
            {
                m_cancelCurrentAction = true;
                m_errorTitle = QObject::tr("Could not find a suitable name to backup");
                m_errorMsg   = entry->reversedOrder.at(
                            entry->reversedOrder.count() -1
                            ).absoluteFilePath();
            }
        }
#if DEBUG_MESSAGES
        else
        {
            qDebug() <<  entry->reversedOrder.at(entry->reversedOrder.count() -1).absoluteFilePath()
                     << " already exists and will be overwritten";
        }
#endif
    }

    for(; !m_cancelCurrentAction  && !scheduleAnySlot     &&
          entry->currStep       < STEP_FILES              &&
          m_curAction->currItem < m_curAction->totalItems &&
          entry->currItem       < entry->reversedOrder.count()
        ; entry->currStep++,    entry->currItem++
        )

    {
        const QFileInfo &fi = entry->reversedOrder.at(entry->currItem);
        QString orig    = fi.absoluteFilePath();
        QString target = targetFom(orig, m_curAction);
        QString path(target);
        // do this here to allow progress send right item number, copySingleFile will emit progress()
        m_curAction->currItem++;
        //--
        if (fi.isFile() || fi.isSymLink())
        {
            QFileInfo  t(target);
            path = t.path();
        }
        //check if the main item in the entry is a directory
        //if so it needs to appear on any attached view
        if (   m_curAction->currItem == 1
            && entry->reversedOrder.last().isDir()
            && !entry->reversedOrder.last().isSymLink()
           )
        {
            QString entryDir = targetFom(entry->reversedOrder.last().absoluteFilePath(), m_curAction);
            QDir entryDirObj(entryDir);
            if (!entryDirObj.exists() && entryDirObj.mkpath(entryDir))
            {
                emit added(entryDir);
                entry->added = true;
            }
        }
        QDir d(path);
        if (!d.exists() && !d.mkpath(path))
        {
            m_cancelCurrentAction = true;
            m_errorTitle = QObject::tr("Could not create the directory");
            m_errorMsg   = path;
        }
        else
        if (fi.isSymLink())
        {
            m_cancelCurrentAction = ! copySymLink(target,fi);
            if (m_cancelCurrentAction)
            {
                m_errorTitle = QObject::tr("Could not create link to");
                m_errorMsg   = target;
            }
            m_curAction->bytesWritten += COMMON_SIZE_ITEM;
        }
        else
        if (fi.isDir())
        {
            m_cancelCurrentAction = !
                 QFile(target).setPermissions(fi.permissions());
            if (m_cancelCurrentAction)
            {
                m_errorTitle = QObject::tr("Could not set permissions to dir");
                m_errorMsg   = target;
            }
            m_curAction->bytesWritten += COMMON_SIZE_ITEM;
        }
        else
        if (fi.isFile())
        {
            qint64 needsSize = 0;
            m_curAction->copyFile.clear();
            m_curAction->copyFile.source = new QFile(orig);
            m_cancelCurrentAction = !m_curAction->copyFile.source->open(QFile::ReadOnly);
            if (m_cancelCurrentAction)
            {               
                m_errorTitle = QObject::tr("Could not open file");
                m_errorMsg   = orig;
            }
            else
            {
                needsSize = m_curAction->copyFile.source->size();
                //create destination
                m_curAction->copyFile.target = new QFile(target);             
                m_curAction->copyFile.targetName = target;
                //first open it read-only to get its size if exists
                if (m_curAction->copyFile.target->open(QFile::ReadOnly))
                {
                    needsSize -= m_curAction->copyFile.target->size();
                    m_curAction->copyFile.target->close();
                }
                //check if there is disk space to copy source to target
                if (needsSize > 0 && !isThereDiskSpace( needsSize ))
                {
                    m_cancelCurrentAction = true;
                    m_errorTitle = QObject::tr("There is no space on disk to copy");
                    m_errorMsg   =  m_curAction->copyFile.target->fileName();
                }
            }
            if (!m_cancelCurrentAction)
            {
                m_cancelCurrentAction =
                        !m_curAction->copyFile.target->open(QFile::WriteOnly | QFile::Truncate);
                if (m_cancelCurrentAction)
                {
                    m_errorTitle = QObject::tr("Could not create file");
                    m_errorMsg   =  m_curAction->copyFile.target->fileName();
                }
            }
            if (!m_cancelCurrentAction)
            {
                m_curAction->copyFile.isEntryItem = entry->currItem  == (entry->reversedOrder.count() -1);
                scheduleAnySlot =  processCopySingleFile();
                //main item from the entry. notify views new item inserted,
                //depending on the file size it may take longer, the view needs to be informed
                if (m_curAction->copyFile.isEntryItem && !m_cancelCurrentAction)
                {
                    if (!entry->alreadyExists)
                    {
                       emit added(target);
                       entry->added = true;
                    }
                    else
                    {
                        emit changed(QFileInfo(target));
                    }
                }
            }
        }//end isFile
    }//for

    //no copy going on
    if (!scheduleAnySlot)
    {
        endActionEntry();
    }
}


//===============================================================================================
/*!
 * \brief FileSystemAction::moveEntry
 * \param entry
 */
void FileSystemAction::moveEntry(ActionEntry *entry)
{
    QFile file;

    for(; !m_cancelCurrentAction                          &&
          entry->currStep       < STEP_FILES              &&
          m_curAction->currItem < m_curAction->totalItems &&
          entry->currItem       < entry->reversedOrder.count()
        ; entry->currStep++,    m_curAction->currItem++, entry->currItem++
        )

    {
        const QFileInfo &fi = entry->reversedOrder.at(entry->currItem);
        file.setFileName(fi.absoluteFilePath());
        QString target(targetFom(fi.absoluteFilePath(), m_curAction));
        QFileInfo targetInfo(target);
        //rename will fail
        if (targetInfo.exists())
        {
            //will not emit removed() neither added()
            entry->added = true;
            if (targetInfo.isFile() || targetInfo.isSymLink())
            {
                if (!QFile::remove(target))
                {
                    m_cancelCurrentAction = true;
                    m_errorTitle = QObject::tr("Could remove the directory/file ") + target;
                    m_errorMsg   = ::strerror(errno);
                }
            }
            else
            if (targetInfo.isDir())
            {
               //move target to /tmp and remove it later by creating an Remove action
               //this will emit removed()
               moveDirToTempAndRemoveItLater(target);
            }
        }
        if (!m_cancelCurrentAction && !file.rename(target))
        {
            m_cancelCurrentAction = true;
            m_errorTitle = QObject::tr("Could not move the directory/file ") + target;
            m_errorMsg   = ::strerror(errno);
        }
    }//for
}

//===============================================================================================
/*!
 * \brief FileSystemAction::pathChanged
 * \param path
 */
void FileSystemAction::pathChanged(const QString &path)
{
    m_path = path;
}



void FileSystemAction::copyIntoCurrentPath(const QStringList& items)
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << items;
#endif
    m_clipboardChanged = false;
    if (items.count())
    {
        QFileInfo destination(m_path);
        if (destination.isWritable())
        {
            createAndProcessAction(ActionCopy, items);
        }
        else
        {
            emit error(tr("Cannot copy items"),
                       tr("no write permission on folder ") + destination.absoluteFilePath() );

        }
    }
}


void FileSystemAction::moveIntoCurrentPath(const QStringList& items)
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << items;
#endif
    m_clipboardChanged = false;
    if (items.count())
    {
        QFileInfo destination(m_path);
        QFileInfo origin(QFileInfo(items.at(0)).absolutePath());
        ActionType actionType  = ActionMove;
        static QString titleError     = tr("Cannot move items");
        static QString noWriteError   = tr("no write permission on folder ");
        //we allow Copy to backup items, but Cut must Fail
        if (destination.absoluteFilePath() == origin.absoluteFilePath())
        {
            emit error(titleError,
                       tr("origin and destination folders are the same"));
            return;
        }
        // cut needs write permission on origin
        if (!origin.isWritable())
        {
            emit error(titleError, noWriteError + origin.absoluteFilePath());
            return;
        }
        //check if it is possible to move items
        if ( !moveUsingSameFileSystem(items.at(0)) )
        {
            actionType = ActionHardMoveCopy; // first step
        }
        if (!destination.isWritable())
        {
            emit error(titleError, noWriteError + destination.absoluteFilePath());
            return;
        }
        createAndProcessAction(actionType, items);
    }
}


//===============================================================================================
/*!
 * \brief FileSystemAction::createAndProcessAction
 * \param actionType
 * \param paths
 * \param operation
 */
void  FileSystemAction::createAndProcessAction(ActionType actionType, const QStringList& paths)
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << paths;
#endif
    Action       *myAction       = 0;
    int           origPathLen    = 0;
    myAction                     = createAction(actionType, origPathLen);
    myAction->origPath           = QFileInfo(paths.at(0)).absolutePath();
    myAction->baseOrigSize       = myAction->origPath.length();
    for (int counter=0; counter < paths.count(); counter++)
    {
        addEntry(myAction, paths.at(counter));
    }
    if (myAction->totalItems > 0)
    {
        if (actionType == ActionHardMoveCopy)
        {
            myAction->totalItems *= 2; //duplicate this
        }
        /*
        if (actionType == ActionHardMoveCopy || actionType == ActionCopy)
        {
            //if a file size is less than (COPY_BUFFER_SIZE * STEP_FILES) a single step does that
            //and it is already computed
            myAction->steps +=  myAction->totalBytes / (COPY_BUFFER_SIZE * STEP_FILES);
        }
        */        
        m_queuedActions.append(myAction);
        if (!m_busy)
        {
            processAction();
        }
    }
    else
    {   // no items were added into the Action, maybe items were removed
        //addEntry() emits error() signal when items do not exist
        delete myAction;
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO << "Action is empty, no work to do";
#endif
    }
}


//===============================================================================================
/*!
 * \brief FileSystemAction::targetFom() makes a destination full pathname from \a origItem
 * \param origItem full pathname from a item intended to be copied or moved into current path
 * \return full pathname of target
 */
QString FileSystemAction::targetFom(const QString& origItem, const Action* const action)
{
    QString destinationUnderTarget(origItem.mid(action->baseOrigSize));
    if (action->currEntry  && action->currEntry->newName)
    {
        int len = destinationUnderTarget.indexOf(QDir::separator(), 1);
        if (len == -1) {
            len = destinationUnderTarget.size();
        }
        destinationUnderTarget.replace(1, len -1, *action->currEntry->newName);
    }
    QString target(action->targetPath + destinationUnderTarget);

#if DEBUG_MESSAGES
     qDebug() << Q_FUNC_INFO << "orig" << origItem
              << "target"    << target;
#endif
    return target;
}


//===============================================================================================
/*!
 * \brief FileSystemAction::moveUsingSameFileSystem() Checks if the item being moved to
 *   current m_path belongs to the same File System
 *
 *  It is used to set ActionHardMoveCopy or ActionMove for cut operations.
 *
 * \param itemToMovePathname  first item being moved from a paste operation
 *
 * \return true if the item being moved to the current m_path belongs to the same file system as m_path
 */
bool FileSystemAction::moveUsingSameFileSystem(const QString& itemToMovePathname)
{
    unsigned long targetFsId = 0xffff;
    unsigned long originFsId = 0xfffe;
#if defined(Q_OS_UNIX)
    struct statvfs  vfs;
    if ( ::statvfs( QFile::encodeName(m_path).constData(), &vfs) == 0 )
    {
        targetFsId = vfs.f_fsid;
    }
    if ( ::statvfs(QFile::encodeName(itemToMovePathname).constData(), &vfs) == 0)
    {
        originFsId = vfs.f_fsid;
    }   
#else
    Q_UNUSED(itemToMovePathname); 
#endif
    return targetFsId == originFsId;
}


//================================================================================
/*!
 * \brief FileSystemAction::endCurrentAction() finishes an Action
 *
 *  If a Paste was made from a Cut operation, items pasted become avaialable in the clipboard
 *   as from Copy source operation, so items can be now Pasted again, but with no source removal
 *
 * It checks for \a m_clipboardChanged that idenftifies if the clipboard was modified during the
 * operation maybe by another application.
 */
void FileSystemAction::endCurrentAction()
{

    if ( !m_clipboardChanged  &&
          m_curAction->origPath != m_curAction->targetPath &&
         (m_curAction->type == ActionMove  || m_curAction->type == ActionHardMoveRemove)
       )
    {
         QStringList items;
         const ActionEntry *entry;
         int   last;
         for(int e = 0; e < m_curAction->entries.count(); e++)
         {
             entry   = m_curAction->entries.at(e);
             last    = entry->reversedOrder.count() -1;
             QString item(targetFom(entry->reversedOrder.at(last).absoluteFilePath(), m_curAction));
             items.append(item);
         }
         if (items.count())
         {
             QString targetPath = m_curAction->targetPath;
             //it is not necessary to handle own clipboard here
             emit recopy(items, targetPath);
         }
    }
}

//================================================================================
/*!
 * \brief FileSystemAction::copySingleFile() do a single file copy
 *
 * Several write operations are required to copy big files, each operation writes (STEP_FILES * 4k) bytes.
 * After a write operation if more operations are required to copy the whole file,
 * a progress() signal is emitted and a new write operation is scheduled to happen in the next loop interaction.
 *
 * \return  true if scheduled to another slot either processCopyEntry() or itself; false if not.
 */
bool FileSystemAction::processCopySingleFile()
{
#if DEBUG_MESSAGES
        qDebug() << Q_FUNC_INFO;
#endif
    char block[COPY_BUFFER_SIZE];
    int  step = 0;
    bool copySingleFileDone = false;
    bool scheduleAnySlot    = true;
    int  startBytes         = m_curAction->copyFile.bytesWritten;

    while( m_curAction->copyFile.source           &&
           !m_curAction->copyFile.source->atEnd() &&
           !m_cancelCurrentAction                 &&
           m_curAction->copyFile.bytesWritten < m_curAction->copyFile.source->size() &&
           step++ < STEP_FILES
         )
    {
        qint64 in = m_curAction->copyFile.source->read(block, sizeof(block));
        if (in > 0)
        {
            if(in != m_curAction->copyFile.target->write(block, in))
            {
                  m_curAction->copyFile.source->close();
                  m_curAction->copyFile.target->close();
                  m_cancelCurrentAction = true;
                  m_errorTitle = QObject::tr("Write error in ")
                                  + m_curAction->copyFile.targetName,
                  m_errorMsg   = ::strerror(errno);
                  break;
            }
            m_curAction->bytesWritten          += in;
            m_curAction->copyFile.bytesWritten += in;
            if (m_curAction->copyFile.isEntryItem)
            {
                m_curAction->copyFile.amountSavedToRefresh -= in;
            }
        }
        else
        if (in < 0)
        {
           m_cancelCurrentAction = true;
           m_errorTitle = QObject::tr("Read error in ")
                           + m_curAction->copyFile.source->fileName();
           m_errorMsg   = ::strerror(errno);
           break;
        }
    }// end write loop

    // write loop finished, the copy might be finished
    if (!m_cancelCurrentAction
        && m_curAction->copyFile.source
        && m_curAction->copyFile.bytesWritten == m_curAction->copyFile.source->size()
        && m_curAction->copyFile.source->isOpen()
       )
    {
        copySingleFileDone = endCopySingleFile();
    }

    if (m_cancelCurrentAction)
    {
        if (m_curAction->copyFile.target)
        {
            if (m_curAction->copyFile.target->isOpen())
            {
                   m_curAction->copyFile.target->close();
            }
            if (m_curAction->copyFile.target->remove())
            {               
                emit removed(m_curAction->copyFile.targetName);
            }
        }
        m_curAction->copyFile.clear();
        endActionEntry();
    }
    else
    {
        if (copySingleFileDone)
        {
            m_curAction->copyFile.clear();
            //whem the whole copy could be done just in one call
            //do not schedule to call copyEntry()
            if (startBytes > 0)
            {
                //the whole took more than one call to copySingleFile()
                scheduleSlot(SLOT(processCopyEntry()));
            }
            else
            {   //return normally to entry loop
                scheduleAnySlot = false;
            }
        }
        else
        {
            notifyProgress();
            if (m_curAction->copyFile.isEntryItem && m_curAction->copyFile.amountSavedToRefresh <= 0)
            {
                m_curAction->copyFile.amountSavedToRefresh = AMOUNT_COPIED_TO_REFRESH_ITEM_INFO;
                emit changed(QFileInfo(m_curAction->copyFile.targetName));
            }
            scheduleSlot(SLOT(processCopySingleFile()));
        }
    }

    return scheduleAnySlot;
}


//================================================================================
/*!
 * \brief FileSystemAction::percentWorkDone() Compute the percent of work done
 *
 * Copy operations are based on bytes written while remove/move operations are based on items number
 *
 * \return the percent of work done
 */
int FileSystemAction::percentWorkDone()
{
    int percent = 0;

    //copying empty files will have totalBytes==0
    if ( m_curAction->totalBytes > 0 &&
         (m_curAction->type == ActionCopy || m_curAction->type == ActionHardMoveCopy)
       )
    {
        percent = (m_curAction->bytesWritten * 100) / m_curAction->totalBytes ;
    }
    else
    {   //percentage based on number of items performed
        percent = (m_curAction->currItem * 100) / m_curAction->totalItems;
    }

    if (percent > 100)
    {
        percent = 100;
    }
    return percent;
}


//================================================================================
/*!
 * \brief FileSystemAction::notifyProgress() Notify the progress signal
 *
 * \return the percent of work done
 */
int FileSystemAction::notifyProgress(int forcePercent)
{
    int percent = forcePercent > 0 ? forcePercent :  percentWorkDone();
    if (percent == 0)
    {
        percent = 1;
    }
    if (SHOULD_EMIT_PROGRESS_SIGNAL(m_curAction) && !m_curAction->done)
    {
        if (m_curAction->type == ActionHardMoveCopy ||
            m_curAction->type ==ActionHardMoveRemove)
        {
            emit progress(m_curAction->currItem/2,  m_curAction->totalItems/2, percent);
        }
        else
        {
            emit progress(m_curAction->currItem,  m_curAction->totalItems, percent);
        }
        if (percent == 100 && m_curAction->currItem == m_curAction->totalItems)
        {
            m_curAction->done = true;
        }
    }
    return  percent;
}

//================================================================================
/*!
 * \brief FileSystemAction::copySymLink() creates the \a target as a link according to \a orig
 * \param target full pathname of the file to be created
 * \param orig   original file, it carries the link that \a target will point to
 * \return true if it could create, else if not
 */
bool FileSystemAction::copySymLink(const QString &target, const QFileInfo &orig)
{
    QString link(orig.symLinkTarget());
    QFileInfo linkFile(link);
    if (linkFile.isAbsolute() && linkFile.absolutePath() == orig.absolutePath())
    {
        link = linkFile.fileName();
    }
#if QT_VERSION <= 0x040704
    QString current = QDir::currentPath();
    QDir::setCurrent(linkFile.absolutePath());
    bool ret = QFile::link(link, target);
    QDir::setCurrent(current);
#else
    bool ret = QFile::link(link, target);
#endif
#if DEBUG_MESSAGES
    qDebug() << Q_FUNC_INFO << ret << target << link;
#endif
    return ret;
}

//================================================================================
void FileSystemAction::scheduleSlot(const char *slot)
{
#if DEBUG_MESSAGES
    qDebug() << Q_FUNC_INFO << slot;
#endif
    QTimer::singleShot(0, this, slot);
}



//================================================================================
/*!
 * \brief FileSystemAction::moveDirToTempAndRemoveItLater() moves a directory to temp and shedules it for be removed later
 *
 * When pasting from cut actions, directories will be totally replaced, when they already exist, they need to be removed
 * before moving the new content, so the solution is to move them to temp directory and create another action to remove
 * them later, after that the content is moved to a target that does not exist any more.
 *
 * \param dir directory name which is the target for paste operation and needs get removed first
 */
void FileSystemAction::moveDirToTempAndRemoveItLater(const QString& dir)
{
    QString tempDir;
    {
        //create this temporary file just to get a unique name
        QTemporaryFile d;
        d.setAutoRemove(true);
        d.open();
        d.close();
        tempDir = d.fileName();
    }
#if defined(DEBUG_MESSAGES) || defined(REGRESSION_TEST_FOLDERLISTMODEL)
    qDebug() << Q_FUNC_INFO << dir <<  "being moved to" << tempDir;
#endif
    if (QFile::rename(dir, tempDir))
    {
        if (!m_curAction->auxAction)
        {   // this new action as Remove will remove all dirs
            m_curAction->auxAction            = createAction(ActionRemove);
            m_curAction->auxAction->isAux     = true;
            m_queuedActions.append(m_curAction->auxAction);
        }
        addEntry(m_curAction->auxAction, tempDir);
    }
}

//================================================================================
/*!
 * \brief FileSystemAction::isBusy() just inform if there is any Action going on
 * \return  true when there is any Action going on
 */
bool FileSystemAction::isBusy() const
{
    return m_busy;
}

//==================================================================
/*!
 * \brief FileSystemAction::makeBackupNameForCurrentItem() creates a new name suitable for backup an item
 *
 * The item can be a folder or a single file, but it is an Entry that means it is under the path were Copy happened
 * The newName field from current entry will be set to a suitable name
 * \param action
 */
bool FileSystemAction::makeBackupNameForCurrentItem(Action *action)
{
    bool ret = false;
    if (action->currEntry->alreadyExists)
    {
        const QFileInfo& fi =
              action->currEntry->reversedOrder.at(action->currEntry->reversedOrder.count() -1);
        QFileInfo backuped;       
        int counter=0;
        QString name;
        do
        {
            QString copy(QObject::tr(" Copy"));
            if(++counter > 0)
            {
                copy += QLatin1Char('(') +
                        QString::number(counter) +
                        QLatin1Char(')');
            }
            name = fi.fileName();
            int  pos = name.size();
            if (!fi.isDir())
            {
                int dot = name.lastIndexOf(QChar('.'));
                if (dot != -1)
                {
                    pos = dot;
                }
            }
            name.insert(pos,copy);
            backuped.setFile(fi.absoluteDir(), name);
        } while (backuped.exists() && counter < 100);
        if (counter < 100)
        {
            action->currEntry->newName = new QString(backuped.fileName());
            ret = true;
        }
    }
    return ret;
}

//==================================================================
/*!
 * \brief FileSystemAction::getProgressCounter
 * \return number of progress notification from current Action
 */
int FileSystemAction::getProgressCounter() const
{
    int steps = 0;
    if (m_curAction)
    {
        steps = m_curAction->steps;
    }
    return steps;
}


//==================================================================
bool FileSystemAction::endCopySingleFile()
{
    bool ret = true;
    m_curAction->copyFile.source->close();
    m_curAction->copyFile.target->close();
    m_cancelCurrentAction = !m_curAction->copyFile.target->setPermissions(
                                 m_curAction->copyFile.source->permissions());
    if (m_cancelCurrentAction)
    {
        m_errorTitle = QObject::tr("Set permissions error in ")
                        + m_curAction->copyFile.targetName,
        m_errorMsg   = ::strerror(errno);
        ret          = false;
    }
    return ret;
}

//==================================================================
bool FileSystemAction::isThereDiskSpace(qint64 requiredSize)
{
    bool ret = true;
#if defined(Q_OS_UNIX)
    struct statvfs  vfs;
    if ( ::statvfs( QFile::encodeName(m_path).constData(), &vfs) == 0 )
    {
        qint64 free =  vfs.f_bsize * vfs.f_bfree;
        ret = free > requiredSize;
    }
#endif
   return ret;
}


//==================================================================
/*!
 * \brief FileSystemAction::onClipboardChanged()
 *
 *  sets \ref m_clipboardChanged indicating the fhe Clipboard was changed.
 */
void FileSystemAction::onClipboardChanged()
{
    m_clipboardChanged = true;
}
