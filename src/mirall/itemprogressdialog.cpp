/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include <QtGui>

#include "mirall/itemprogressdialog.h"
#include "mirall/syncresult.h"
#include "mirall/logger.h"
#include "mirall/utility.h"
#include "mirall/theme.h"
#include "mirall/folderman.h"
#include "mirall/syncfileitem.h"

#include "ui_itemprogressdialog.h"

namespace Mirall {

ItemProgressDialog::ItemProgressDialog(Application*, QWidget *parent) :
    QDialog(parent),
    ErrorIndicatorRole( Qt::UserRole +1 ),
    _ui(new Ui::ItemProgressDialog)
{
    _ui->setupUi(this);
    connect(_ui->_dialogButtonBox->button(QDialogButtonBox::Close), SIGNAL(clicked()),
            this, SLOT(accept()));

    connect(ProgressDispatcher::instance(), SIGNAL(progressInfo(QString,Progress::Info)),
            this, SLOT(slotProgressInfo(QString,Progress::Info)));
    connect(ProgressDispatcher::instance(), SIGNAL(progressSyncProblem(const QString&,const Progress::SyncProblem&)),
            this, SLOT(slotProgressErrors(const QString&, const Progress::SyncProblem&)));

    QStringList header;
    header << tr("Folder/Time");
    header << tr("File");
    header << tr("Action");
    header << tr("Size");

    _ui->_treeWidget->setHeaderLabels( header );

    _ui->_treeWidget->setColumnWidth(1, 180);

    connect(this, SIGNAL(guiLog(QString,QString)), Logger::instance(), SIGNAL(guiLog(QString,QString)));

    QPushButton *copyBtn = _ui->_dialogButtonBox->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    connect(copyBtn, SIGNAL(clicked()), SLOT(copyToClipboard()));

    setWindowTitle(tr("Sync Protocol"));

}

void ItemProgressDialog::setSyncResultStatus(const SyncResult& result )
{
    QString folderMessage;

    SyncResult::Status syncStatus = result.status();
    switch( syncStatus ) {
    case SyncResult::Undefined:
        folderMessage = tr( "Undefined Folder State" );
        break;
    case SyncResult::NotYetStarted:
        folderMessage = tr( "The folder waits to start syncing." );
        break;
    case SyncResult::SyncPrepare:
        folderMessage = tr( "Determining which files to sync." );
        break;
    case SyncResult::Unavailable:
        folderMessage = tr( "Server is currently not available." );
        break;
    case SyncResult::SyncRunning:
        folderMessage = tr("Sync is running.");
        break;
    case SyncResult::Success:
        folderMessage = tr("Last Sync was successful.");
        break;
    case SyncResult::Error:
        folderMessage = tr( "Syncing Error." );
        break;
    case SyncResult::SetupError:
        folderMessage = tr( "Setup Error." );
        break;
    case SyncResult::Problem:
        folderMessage = tr( "Sync success, but warnings on individual files.");
        break;
    default:
        folderMessage = tr( "Undefined Error State." );
    }

    _ui->_timelabel->setText(tr("%1").arg(folderMessage));

    if( result.errorStrings().count() ) {
        _ui->_errorLabel->setVisible(true);
        _ui->_errorLabel->setTextFormat(Qt::RichText);
        QString errStr;
        foreach( QString err, result.errorStrings() ) {
            errStr.append(QString("<p>%1</p>").arg(err));
        }

        _ui->_errorLabel->setText(errStr);
    } else {
        _ui->_errorLabel->setText(QString::null);
        _ui->_errorLabel->setVisible(false);
    }

}

void ItemProgressDialog::setSyncResult( const SyncResult& result )
{
    setSyncResultStatus(result);

    const QString& folder = result.folder();
    qDebug() << "Setting sync result for folder " << folder;

    QTreeWidgetItem *folderItem = findFolderItem(folder);
    if( ! folderItem ) return;

    SyncFileItemVector::const_iterator i;
    const SyncFileItemVector& items = result.syncFileItemVector();
    QDateTime dt = QDateTime::currentDateTime();

    for (i = items.begin(); i != items.end(); ++i) {
         const SyncFileItem& item = *i;
         QString errMsg;
         if( item._instruction == CSYNC_INSTRUCTION_IGNORE ) {
             QStringList columns;
             QString timeStr = timeString(dt);
             QString longTimeStr = timeString(dt, QLocale::LongFormat);

             columns << timeStr;
             columns << item._file;
             if( item._type == SyncFileItem::File ) {
                 errMsg = tr("File ignored.");
             } else if( item._type == SyncFileItem::Directory ){
                 errMsg = tr("Directory ignored.");
             } else if( item._type == SyncFileItem::SoftLink ) {
                 errMsg = tr("Soft Link ignored.");
             } else {
                 errMsg = tr("Ignored.");
             }
             columns << errMsg;

             QTreeWidgetItem *twitem = new QTreeWidgetItem(folderItem, columns);
             twitem->setData(0, ErrorIndicatorRole, QVariant(true) );
             twitem->setToolTip(0, longTimeStr);
             twitem->setIcon(0, Theme::instance()->syncStateIcon(SyncResult::Problem, true));

             Q_UNUSED(twitem);
         }
    }
}

void ItemProgressDialog::setupList()
{
  // get the folders to set up the top level list.
  Folder::Map map = FolderMan::instance()->map();
  SyncResult lastResult;
  QDateTime dt;
  bool haveSyncResult = false;

  foreach( Folder *f, map.values() ) {
    findFolderItem(f->alias());
    if( f->syncResult().syncTime() > dt ) {
        dt = f->syncResult().syncTime();
        lastResult = f->syncResult();
        haveSyncResult = true;
    }
  }

  if( haveSyncResult ) {
      setSyncResult(lastResult);
  }

  QList<Progress::Info> progressList = ProgressDispatcher::instance()->recentChangedItems(0); // All.

  QHash <QString, int> folderHash;

  foreach( Progress::Info info, progressList ) {
    slotProgressInfo( info.folder, info );
    folderHash[info.folder] = 1;
  }

  QList<Progress::SyncProblem> problemList = ProgressDispatcher::instance()->recentProblems(0);
  foreach( Progress::SyncProblem prob, problemList ) {
    slotProgressErrors(prob.folder, prob);
    folderHash[prob.folder] = 1;
  }

  foreach( const QString& folder, folderHash.keys() ) {
    decorateFolderItem(folder);
  }

}

ItemProgressDialog::~ItemProgressDialog()
{
    delete _ui;
}

void ItemProgressDialog::copyToClipboard()
{
    QString text;
    QTextStream ts(&text);

    int topLevelItems = _ui->_treeWidget->topLevelItemCount();
    for (int i = 0; i < topLevelItems; i++) {
        QTreeWidgetItem *item = _ui->_treeWidget->topLevelItem(i);
        ts << left << qSetFieldWidth(0)
           << item->data(0, Qt::DisplayRole).toString()
           << endl;
        int childItems = item->childCount();
        for (int j = 0; j < childItems; j++) {
            QTreeWidgetItem *child =item->child(j);
                // time stamp
            ts << left << qSetFieldWidth(10)
               << child->data(0,Qt::DisplayRole).toString()
                  // file name
               << qSetFieldWidth(50)
               << child->data(1,Qt::DisplayRole).toString()
                  // action
               << qSetFieldWidth(15)
               << child->data(2, Qt::DisplayRole).toString()
                  // size
               << qSetFieldWidth(10)
               << child->data(3, Qt::DisplayRole).toString()
               << qSetFieldWidth(0)
               << endl;
        }
    }

    QApplication::clipboard()->setText(text);
    emit guiLog(tr("Copied to clipboard"), tr("The sync protocol has been copied to the clipboard."));
}

void ItemProgressDialog::accept()
{
    QDialog::accept();
}

void ItemProgressDialog::decorateFolderItem( const QString& folder )
{
  QTreeWidgetItem *folderItem = findFolderItem(folder);
  if( ! folderItem ) return;
  int errorCnt = 0;

  int childCnt = folderItem->childCount();
  for( int cnt = 0; cnt < childCnt; cnt++ ) {
    bool isErrorItem = folderItem->child(cnt)->data(0, ErrorIndicatorRole).toBool();
    if( isErrorItem ) {
      errorCnt++;
    }
  }

  if( errorCnt == 0 ) {
    folderItem->setIcon(0, Theme::instance()->syncStateIcon(SyncResult::Success));
  } else {
    // FIXME: Set a soft error icon here.
    folderItem->setIcon(0, Theme::instance()->syncStateIcon(SyncResult::Problem));
  }
}

QTreeWidgetItem *ItemProgressDialog::createFolderItem(const QString& folder)
{
    QStringList strings;
    strings.append(folder);
    QTreeWidgetItem *item = new QTreeWidgetItem( _ui->_treeWidget, strings );
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    return item;
}

QTreeWidgetItem *ItemProgressDialog::findFolderItem( const QString& folder )
{
  QTreeWidgetItem *folderItem;

  if( folder.isEmpty() ) return NULL;

  if( !_folderItems.contains(folder)) {
      _folderItems[folder] = createFolderItem(folder);
      _ui->_treeWidget->addTopLevelItem(_folderItems[folder]);
  }
  folderItem = _folderItems[folder];

  return folderItem;
}

void ItemProgressDialog::cleanErrors( const QString& folder )
{
  _problemCounter = 0;
  QList<QTreeWidgetItem*> wipeList;

  QTreeWidgetItem *folderItem = findFolderItem(folder);
  if( ! folderItem ) return;

  int childCnt = folderItem->childCount();
  for( int cnt = 0; cnt < childCnt; cnt++ ) {
    bool isErrorItem = folderItem->child(cnt)->data(0, ErrorIndicatorRole).toBool();
    if( isErrorItem ) {
      wipeList.append(folderItem->child(cnt));
    }
  }
  qDeleteAll(wipeList.begin(), wipeList.end());
}

QString ItemProgressDialog::timeString(QDateTime dt, QLocale::FormatType format) const
{
    QLocale loc = QLocale::system();
    QString timeStr;
    QDate today = QDate::currentDate();

    if( format == QLocale::NarrowFormat ) {
        if( dt.date().day() == today.day() ) {
            timeStr = loc.toString(dt.time(), QLocale::NarrowFormat);
        } else {
            timeStr = loc.toString(dt, QLocale::NarrowFormat);
        }
    } else {
        timeStr = loc.toString(dt, format);
    }
    return timeStr;
}

void ItemProgressDialog::slotProgressErrors( const QString& folder, const Progress::SyncProblem& problem )
{
  QTreeWidgetItem *folderItem;

  folderItem = findFolderItem(folder);
  if( !folderItem ) return;

  QStringList columns;
  QString timeStr = timeString(problem.timestamp);
  QString longTimeStr = timeString(problem.timestamp, QLocale::LongFormat);

  columns << timeStr;
  columns << problem.current_file;
  QString errMsg = tr("Problem: %1").arg(problem.error_message);
  columns << errMsg;
  // FIXME: Show the error code if available.

  QTreeWidgetItem *item = new QTreeWidgetItem(folderItem, columns);
  item->setData(0, ErrorIndicatorRole, QVariant(true) );
  item->setIcon(0, Theme::instance()->syncStateIcon(SyncResult::Problem, true));
  item->setToolTip(0, longTimeStr);
  Q_UNUSED(item);
}

void ItemProgressDialog::slotProgressInfo( const QString& folder, const Progress::Info& progress )
{
    QTreeWidgetItem *folderItem;
    folderItem = findFolderItem(folder);
    if( !folderItem ) return;

    if( progress.kind == Progress::StartSync ) {
      cleanErrors( folder );
      folderItem->setIcon(0, Theme::instance()->syncStateIcon(SyncResult::SyncRunning));
    }

    if( progress.kind == Progress::EndSync ) {
      decorateFolderItem( folder );
    }

    // Ingore other events than finishing an individual up- or download.
    if( !(progress.kind == Progress::EndDownload || progress.kind == Progress::EndUpload || progress.kind == Progress::EndDelete)) {
        return;
    }

    QStringList columns;
    QString timeStr = timeString(progress.timestamp);
    QString longTimeStr = timeString(progress.timestamp, QLocale::LongFormat);

    columns << timeStr;
    columns << progress.current_file;
    columns << Progress::asResultString(progress.kind);
    columns << Utility::octetsToString( progress.file_size );

    QTreeWidgetItem *item = new QTreeWidgetItem(folderItem, columns);
    item->setToolTip(0, longTimeStr);
    Q_UNUSED(item);
}


}
