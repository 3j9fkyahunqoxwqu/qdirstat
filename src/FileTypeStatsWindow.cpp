/*
 *   File name: FileTypeStatsWindow.cpp
 *   Summary:	QDirStat file type statistics window
 *   License:	GPL V2 - See file LICENSE for details.
 *
 *   Author:	Stefan Hundhammer <Stefan.Hundhammer@gmx.de>
 */


#include "FileTypeStatsWindow.h"
#include "FileTypeStats.h"
#include "LocateFilesWindow.h"
#include "DirTree.h"
#include "MimeCategory.h"
#include "SettingsHelpers.h"
#include "Logger.h"
#include "Exception.h"

#include <algorithm>


using namespace QDirStat;

// Number of suffixes in the "other" category
#define TOP_X	20

#if (QT_VERSION < QT_VERSION_CHECK( 5, 0, 0 ))
#  define setSectionResizeMode setResizeMode
#endif


QPointer<LocateFilesWindow> FileTypeStatsWindow::_locateFilesWindow = 0;


FileTypeStatsWindow::FileTypeStatsWindow( DirTree *	   tree,
					  SelectionModel * selectionModel,
					  QWidget *	   parent ):
    QDialog( parent ),
    _ui( new Ui::FileTypeStatsWindow ),
    _tree( tree ),
    _selectionModel( selectionModel )
{
    // logDebug() << "init" << endl;

    CHECK_NEW( _ui );
    _ui->setupUi( this );
    initWidgets();
    readWindowSettings( this, "FileTypeStatsWindow" );

    connect( _ui->treeWidget,	 SIGNAL( currentItemChanged( QTreeWidgetItem *,
							     QTreeWidgetItem * ) ),
	     this,		 SLOT  ( enableLocateButton( QTreeWidgetItem * ) ) );

    connect( _ui->treeWidget,	 SIGNAL( itemDoubleClicked ( QTreeWidgetItem *, int ) ),
	     this,		 SLOT  ( locateCurrentFileType()		      ) );

    connect( _ui->refreshButton, SIGNAL( clicked() ),
	     this,		 SLOT  ( refresh() ) );

    connect( _ui->locateButton,	 SIGNAL( clicked() ),
	     this,		 SLOT  ( locateCurrentFileType() ) );

    _stats = new FileTypeStats( tree, this );
    CHECK_NEW( _stats );

    populate();
}


FileTypeStatsWindow::~FileTypeStatsWindow()
{
    // logDebug() << "destroying" << endl;
    writeWindowSettings( this, "FileTypeStatsWindow" );
}


void FileTypeStatsWindow::clear()
{
    _stats->clear();
    _ui->treeWidget->clear();
    _ui->locateButton->setEnabled( false );
}


void FileTypeStatsWindow::initWidgets()
{
    _ui->treeWidget->setColumnCount( FT_ColumnCount );
    _ui->treeWidget->setHeaderLabels( QStringList()
				      << tr( "Name" )
				      << tr( "Number" )
				      << tr( "Total Size" )
				      << tr( "Percentage" ) );
    _ui->treeWidget->header()->setStretchLastSection( false );

    // Set auto-resize for all columns

    for ( int col = 0; col < FT_ColumnCount; ++col )
    {
	_ui->treeWidget->header()->setSectionResizeMode( col, QHeaderView::ResizeToContents );
    }
}


void FileTypeStatsWindow::refresh()
{
    calc();
}


void FileTypeStatsWindow::calc()
{
    clear();
    _stats->calc();
    populate();
}


void FileTypeStatsWindow::populate()
{
    _ui->treeWidget->clear();

    if ( ! _tree || ! _tree->root() )
    {
	logWarning() << "No tree" << endl;
	return;
    }

    _ui->treeWidget->setSortingEnabled( false );

    //
    // Create toplevel items for the categories
    //

    QMap<MimeCategory *, CategoryFileTypeItem *> categoryItem;
    CategoryFileTypeItem * otherCategoryItem = 0;

    for ( CategoryFileSizeMapIterator it = _stats->categorySumBegin();
	  it != _stats->categorySumEnd();
	  ++it )
    {
	MimeCategory * category = it.key();

	if ( category )
	{
	    FileSize sum	= it.value();
	    int	     count	= _stats->categoryCount( category );
	    double   percentage = _stats->percentage( sum );

	    CategoryFileTypeItem * item =
		new CategoryFileTypeItem( category, count, sum, percentage );
	    CHECK_NEW( item );

	    _ui->treeWidget->addTopLevelItem( item );
	    item->setBold();
	    categoryItem[ category ] = item;

	    if ( category == _stats->otherCategory() )
		otherCategoryItem = item;
	}
    }

    // Prepare to collect items for a category "other"

    QList<FileTypeItem *> otherItems;
    int	     otherCount = 0;
    FileSize otherSum	= 0LL;


    //
    // Create items for each individual suffix (below a category)
    //

    for ( StringFileSizeMapIterator it = _stats->suffixSumBegin();
	  it != _stats->suffixSumEnd();
	  ++it )
    {
	QString	 suffix	    = it.key();
	FileSize sum	    = it.value();
	int	 count	    = _stats->suffixCount( suffix );
	double	 percentage = _stats->percentage( sum );

	MimeCategory * parentCategory = _stats->category( suffix );

	SuffixFileTypeItem * item = new SuffixFileTypeItem( suffix, count, sum, percentage );
	CHECK_NEW( item );


	if ( parentCategory )
	{
	    QTreeWidgetItem * parentItem = categoryItem.value( parentCategory, 0 );

	    if ( parentItem )
		parentItem->addChild( item );
	    else
	    {
		logError() << "ERROR: No parent category item for " << suffix << endl;
		otherItems << item;
		otherCount += count;
		otherSum   += sum;
	    }
	}
	else // No category for this suffix
	{
	    otherItems << item;
	    otherCount += count;
	    otherSum   += sum;
	}
    }

    // Put remaining "other" items below a separate category

    if ( ! otherItems.isEmpty() )
    {
	FileTypeItemCompare cmp;
	std::sort( otherItems.begin(), otherItems.end(), cmp );
	double	percentage = _stats->percentage( otherSum );
	QString name = otherItems.size() > TOP_X ?
	    tr( "Other (Top %1)" ).arg( TOP_X ) : tr( "Other" );

	if ( ! otherCategoryItem )
	{
	    otherCategoryItem = new CategoryFileTypeItem( _stats->otherCategory(),
							  otherCount,
							  otherSum,
							  percentage );
	    CHECK_NEW( otherCategoryItem );
	}

	otherCategoryItem->setText( 0, name );
	otherCategoryItem->setBold();
	_ui->treeWidget->addTopLevelItem( otherCategoryItem );

	int top_x = qMin( TOP_X, otherItems.size() );

	for ( int i=0; i < top_x; ++i )
	{
	    FileTypeItem * item = otherItems.takeFirst();
	    otherCategoryItem->addChild( item );
	}

	if ( ! otherItems.empty() )
	{
#if 1
	    QStringList suffixes;

	    foreach ( FileTypeItem * item, otherItems )
		suffixes << item->text(0);

	    logDebug() << "Discarding " << otherItems.size()
		       << " suffixes below <other>: "
		       << suffixes.join( ", " )
		       << endl;
#endif
	    qDeleteAll( otherItems );
	}
    }

    _ui->treeWidget->setSortingEnabled( true );
    _ui->treeWidget->sortByColumn( FT_TotalSizeCol, Qt::DescendingOrder );
}


void FileTypeStatsWindow::locateCurrentFileType()
{
    SuffixFileTypeItem * current =
	dynamic_cast<SuffixFileTypeItem *>( _ui->treeWidget->currentItem() );

    if ( ! current )
	return;

    if ( current->suffix() == NO_SUFFIX )
    {
	logWarning() << "Can't locate NO_SUFFIX" << endl;

	if ( _locateFilesWindow )
	    _locateFilesWindow->hide();

	return;
    }

    // logDebug() << "Locating " << current->suffix() << endl;

    if ( ! _locateFilesWindow )
    {
	_locateFilesWindow = new LocateFilesWindow( _tree,
						    _selectionModel,
						    qobject_cast<QWidget *>( parent() ) );
	CHECK_NEW( _locateFilesWindow );
	_locateFilesWindow->show();

	// Not using 'this' as parent so the user can close the file types
	// stats window, but keep the locate files window open; if 'this' were
	// used, the destructor of the file type stats window would
	// automatically delete the locate files window, too since it would be
	// part of its children hierarchy.
	//
	// On the downside, that means we have to actively raise() it because
	// it might get hidden behind the stats window.
    }
    else // Reusing existing window
    {
	_locateFilesWindow->show();
	_locateFilesWindow->raise();
    }

    _locateFilesWindow->locate( current->suffix() );
}


void FileTypeStatsWindow::enableLocateButton( QTreeWidgetItem * currentItem )
{
    bool enabled = false;

    if ( currentItem )
    {
	SuffixFileTypeItem * suffixItem =
	    dynamic_cast<SuffixFileTypeItem *>( currentItem );

	enabled = suffixItem && suffixItem->suffix() != NO_SUFFIX;
    }

    _ui->locateButton->setEnabled( enabled );
}


void FileTypeStatsWindow::reject()
{
    deleteLater();
}




CategoryFileTypeItem::CategoryFileTypeItem( MimeCategory * category,
					    int		   count,
					    FileSize	   totalSize,
					    float	   percentage ):
    FileTypeItem( category->name(),
		  count,
		  totalSize,
		  percentage ),
    _category( category )
{

}


SuffixFileTypeItem::SuffixFileTypeItem( const QString & suffix,
					int		count,
					FileSize	totalSize,
					float		percentage ):
    FileTypeItem( "*." + suffix,
		  count,
		  totalSize,
		  percentage ),
    _suffix( suffix )
{
	if ( suffix == NO_SUFFIX )
	    setText( FT_NameCol,  QObject::tr( "<No Extension>" ) );
	else
	    _suffix = "*." + suffix;
}


FileTypeItem::FileTypeItem( const QString & name,
			    int		    count,
			    FileSize	    totalSize,
			    float	    percentage ):
    QTreeWidgetItem( QTreeWidgetItem::UserType ),
    _name( name ),
    _count( count ),
    _totalSize( totalSize ),
    _percentage( percentage )
{
    QString percentStr;
    percentStr.sprintf( "%02.2f%%", percentage );

    setText( FT_NameCol,       name );
    setText( FT_CountCol,      QString( "%1" ).arg( count ) );
    setText( FT_TotalSizeCol,  formatSize( totalSize ) );
    setText( FT_PercentageCol, percentStr );

    setTextAlignment( FT_NameCol,	Qt::AlignLeft  );
    setTextAlignment( FT_CountCol,	Qt::AlignRight );
    setTextAlignment( FT_TotalSizeCol,	Qt::AlignRight );
    setTextAlignment( FT_PercentageCol, Qt::AlignRight );
}


bool FileTypeItem::operator<(const QTreeWidgetItem & rawOther) const
{
    // Since this is a reference, the dynamic_cast will throw a std::bad_cast
    // exception if it fails. Not catching this here since this is a genuine
    // error which should not be silently ignored.
    const FileTypeItem & other = dynamic_cast<const FileTypeItem &>( rawOther );

    int col = treeWidget() ? treeWidget()->sortColumn() : FT_TotalSizeCol;

    switch ( col )
    {
	case FT_NameCol:	return name()	    < other.name();
	case FT_CountCol:	return count()	    < other.count();
	case FT_TotalSizeCol:	return totalSize()  < other.totalSize();
	case FT_PercentageCol:	return percentage() < other.percentage();
	default:		return QTreeWidgetItem::operator<( rawOther );
    }
}


void FileTypeItem::setBold()
{
    QFont boldFont = font( 0 );
    boldFont.setBold( true );

    for ( int col=0; col < FT_ColumnCount; ++col )
	setFont( col, boldFont );
}
