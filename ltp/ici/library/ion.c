/*
 *	ion.c:	functions common to multiple protocols in the ION stack.
 *
 *	Copyright (c) 2007, California Institute of Technology.
 *	ALL RIGHTS RESERVED.  U.S. Government Sponsorship acknowledged.
 *
 *	Author: Scott Burleigh, JPL
 *
 */

#include "zco.h"
#include "ion.h"
#include "smlist.h"
#include "rfx.h"
#include <time.h>

#ifndef NODE_LIST_SEMKEY
#define NODE_LIST_SEMKEY	(0xeeee1)
#endif

#define	ION_DEFAULT_SM_KEY	((255 * 256) + 1)
#define	ION_SM_NAME		"ionwm"
#define	ION_DEFAULT_SDR_NAME	"ion"

#define timestampInFormat	"%4d/%2d/%2d-%2d:%2d:%2d"
#define timestampOutFormat	"%.4d/%.2d/%.2d-%.2d:%.2d:%.2d"

static void	ionProvideZcoSpace(ZcoAcct acct);

/*	*	*	Datatbase access	 *	*	*	*/

static Sdr	_ionsdr(Sdr *newSdr)
{
	static Sdr	sdr = NULL;

	if (newSdr)
	{
		if (*newSdr == NULL)	/*	Detaching.		*/
		{
			sdr = NULL;
		}
		else			/*	Initializing.		*/
		{
			if (sdr == NULL)
			{
				sdr = *newSdr;
			}
		}
	}

	return sdr;
}

static Object	_iondbObject(Object *newDbObj)
{
	static Object	obj = 0;

	if (newDbObj)
	{
		obj = *newDbObj;
	}

	return obj;
}

static IonDB	*_ionConstants()
{
	static IonDB	buf;
	static IonDB	*db = NULL;
	Sdr		sdr;
	Object		dbObject;

	if (db == NULL)
	{
		/*	Load constants into a conveniently accessed
		 *	structure.  Note that this CANNOT be treated
		 *	as a current database image in later
		 *	processing.					*/

		sdr = _ionsdr(NULL);
		CHKNULL(sdr);
		dbObject = _iondbObject(NULL);
		if (dbObject)
		{
			if (sdr_heap_is_halted(sdr))
			{
				sdr_read(sdr, (char *) &buf, dbObject,
						sizeof(IonDB));
			}
			else
			{
				CHKNULL(sdr_begin_xn(sdr));
				sdr_read(sdr, (char *) &buf, dbObject,
						sizeof(IonDB));
				sdr_exit_xn(sdr);
			}

			db = &buf;
		}
	}

	return db;
}

/*	*	*	Memory access	 *	*	*	*	*/

static int	_ionMemory(int *memmgrIdx)
{
	static int	idx = -1;

	if (memmgrIdx)
	{
		idx = *memmgrIdx;
	}

	return idx;
}

static PsmPartition	_ionwm(sm_WmParms *parms)
{
	static uaddr		ionSmId = 0;
	static PsmView		ionWorkingMemory;
	static PsmPartition	ionwm = NULL;
	static int		memmgrIdx;
	static MemAllocator	wmtake = allocFromIonMemory;
	static MemDeallocator	wmrelease = releaseToIonMemory;
	static MemAtoPConverter	wmatop = ionMemAtoP;
	static MemPtoAConverter	wmptoa = ionMemPtoA;

	if (parms)
	{
		if (parms->wmSize == -1)	/*	Destroy.	*/
		{
			if (ionwm)
			{
				memmgr_destroy(ionSmId, &ionwm);
			}

			ionSmId = 0;
			ionwm = NULL;
			memmgrIdx = -1;
			oK(_ionMemory(&memmgrIdx));
			return NULL;
		}

		/*	Opening ION working memory.			*/

		if (ionwm)			/*	Redundant.	*/
		{
			return ionwm;
		}

		ionwm = &ionWorkingMemory;
		if (memmgr_open(parms->wmKey, parms->wmSize,
				&parms->wmAddress, &ionSmId, parms->wmName,
				&ionwm, &memmgrIdx, wmtake, wmrelease,
				wmatop, wmptoa) < 0)
		{
			putErrmsg("Can't open ION working memory.", NULL);
			return NULL;
		}

		oK(_ionMemory(&memmgrIdx));
	}

	return ionwm;
}

void	*allocFromIonMemory(const char *fileName, int lineNbr, size_t length)
{
	PsmPartition	ionwm = _ionwm(NULL);
	PsmAddress	address;
	void		*block;

	address = Psm_zalloc(fileName, lineNbr, ionwm, length);
	if (address == 0)
	{
		putErrmsg("Can't allocate ION working memory.", itoa(length));
		return NULL;
	}

	block = psp(ionwm, address);
	memset(block, 0, length);
#ifdef HAVE_VALGRIND_VALGRIND_H
    VALGRIND_MALLOCLIKE_BLOCK(block, length, 0, 1);
#endif
	return block;
}

void	releaseToIonMemory(const char *fileName, int lineNbr, void *block)
{
	PsmPartition	ionwm = _ionwm(NULL);

	Psm_free(fileName, lineNbr, ionwm, psa(ionwm, (char *) block));
#ifdef HAVE_VALGRIND_VALGRIND_H
    VALGRIND_FREELIKE_BLOCK(block, 0);
#endif
}

void	*ionMemAtoP(uaddr address)
{
	return (void *) psp(_ionwm(NULL), address);
}

uaddr	ionMemPtoA(void *pointer)
{
	return (uaddr) psa(_ionwm(NULL), pointer);
}

static IonVdb	*_ionvdb(char **name)
{
	static IonVdb	*vdb = NULL;
	PsmAddress	vdbAddress;
	PsmAddress	elt;
	Sdr		sdr;
	PsmPartition	ionwm;
	IonDB		iondb;

	if (name)
	{
		if (*name == NULL)	/*	Terminating.		*/
		{
			vdb = NULL;
			return vdb;
		}

		/*	Attaching to volatile database.			*/

		ionwm = _ionwm(NULL);
		if (psm_locate(ionwm, *name, &vdbAddress, &elt) < 0)
		{
			putErrmsg("Failed searching for vdb.", *name);
			return NULL;
		}

		if (elt)
		{
			vdb = (IonVdb *) psp(ionwm, vdbAddress);
			return vdb;
		}

		/*	ION volatile database doesn't exist yet.	*/

		sdr = _ionsdr(NULL);
		CHKNULL(sdr_begin_xn(sdr));	/*	To lock memory.	*/
		vdbAddress = psm_zalloc(ionwm, sizeof(IonVdb));
		if (vdbAddress == 0)
		{
			sdr_exit_xn(sdr);
			putErrmsg("No space for volatile database.", *name);
			return NULL;
		}

		vdb = (IonVdb *) psp(ionwm, vdbAddress);
		memset((char *) vdb, 0, sizeof(IonVdb));
		if ((vdb->nodes = sm_rbt_create(ionwm)) == 0
		|| (vdb->neighbors = sm_rbt_create(ionwm)) == 0
		|| (vdb->contactIndex = sm_rbt_create(ionwm)) == 0
		|| (vdb->rangeIndex = sm_rbt_create(ionwm)) == 0
		|| (vdb->timeline = sm_rbt_create(ionwm)) == 0
		|| (vdb->probes = sm_list_create(ionwm)) == 0
		|| (vdb->requisitions[0] = sm_list_create(ionwm)) == 0
		|| (vdb->requisitions[1] = sm_list_create(ionwm)) == 0
		|| psm_catlg(ionwm, *name, vdbAddress) < 0)
		{
			sdr_exit_xn(sdr);
			putErrmsg("Can't initialize volatile database.", *name);
			return NULL;
		}

		vdb->clockPid = ERROR;	/*	None yet.		*/
		sdr_read(sdr, (char *) &iondb, _iondbObject(NULL),
				sizeof(IonDB));
		vdb->deltaFromUTC = iondb.deltaFromUTC;
		sdr_exit_xn(sdr);	/*	Unlock memory.		*/
	}

	return vdb;
}

/*	*	*	Initialization	* 	*	*	*	*/

#if defined (FSWLOGGER)
#include "fswlogger.c"
#elif defined (GDSLOGGER)
#include "gdslogger.c"
#else

static void	writeMemoToIonLog(char *text)
{
	static ResourceLock	logFileLock;
	static char		ionLogFileName[264] = "";
	static int		ionLogFile = -1;
	time_t			currentTime = getUTCTime();
	char			timestampBuffer[20];
	int			textLen;
	static char		msgbuf[256];

	if (text == NULL) return;
	if (*text == '\0')	/*	Claims that log file is closed.	*/
	{
		if (ionLogFile != -1)
		{
			close(ionLogFile);	/*	To be sure.	*/
			ionLogFile = -1;
		}

		return;		/*	Ignore zero-length memo.	*/
	}

	/*	The log file is shared, so access to it must be
	 *	mutexed.						*/

	if (initResourceLock(&logFileLock) < 0)
	{
		return;
	}

	lockResource(&logFileLock);
	if (ionLogFile == -1)
	{
		if (ionLogFileName[0] == '\0')
		{
			isprintf(ionLogFileName, sizeof ionLogFileName,
					"%.255s%cion.log",
					getIonWorkingDirectory(),
					ION_PATH_DELIMITER);
		}

		ionLogFile = open(ionLogFileName,
				O_WRONLY | O_APPEND | O_CREAT, 0666);
		if (ionLogFile == -1)
		{
			unlockResource(&logFileLock);
			perror("Can't redirect ION error msgs to log");
			return;
		}
	}

	writeTimestampLocal(currentTime, timestampBuffer);
	isprintf(msgbuf, sizeof msgbuf, "[%s] %s\n", timestampBuffer, text);
	textLen = strlen(msgbuf);
	if (write(ionLogFile, msgbuf, textLen) < 0)
	{
		perror("Can't write ION error message to log file");
	}
#ifdef TargetFFS
	close(ionLogFile);
	ionLogFile = -1;
#endif
	unlockResource(&logFileLock);
}

static void	ionRedirectMemos()
{
	setLogger(writeMemoToIonLog);
}
#endif

#if defined (FSWWATCHER)
#include "fswwatcher.c"
#elif defined (GDSWATCHER)
#include "gdswatcher.c"
#else
static void	ionRedirectWatchCharacters()
{
	setWatcher(NULL);		/*	Defaults to stdout.	*/
}
#endif

static int	checkNodeListParms(IonParms *parms, char *wdName, uvast nodeNbr)
{
	char		*nodeListDir;
	sm_SemId	nodeListMutex;
	char		nodeListFileName[265];
	int		nodeListFile;
	int		lineNbr = 0;
	int		lineLen;
	char		lineBuf[256];
	uvast		lineNodeNbr;
	int		lineWmKey;
	char		lineSdrName[MAX_SDR_NAME + 1];
	char		lineWdName[256];
	int		result;

	nodeListDir = getenv("ION_NODE_LIST_DIR");
	if (nodeListDir == NULL)	/*	Single node on machine.	*/
	{
		if (parms->wmKey == 0)
		{
			parms->wmKey = ION_DEFAULT_SM_KEY;
		}

		if (parms->wmKey != ION_DEFAULT_SM_KEY)
		{
			putErrmsg("Config parms wmKey != default.",
					itoa(ION_DEFAULT_SM_KEY));
			return -1;
		}

		if (parms->sdrName[0] == '\0')
		{
			istrcpy(parms->sdrName, ION_DEFAULT_SDR_NAME,
					sizeof parms->sdrName);
		}

		if (strcmp(parms->sdrName, ION_DEFAULT_SDR_NAME) != 0)
		{
			putErrmsg("Config parms sdrName != default.",
					ION_DEFAULT_SDR_NAME);
			return -1;
		}

		return 0;
	}

	/*	Configured for multi-node operation.			*/

	nodeListMutex = sm_SemCreate(NODE_LIST_SEMKEY, SM_SEM_FIFO);
	if (nodeListMutex == SM_SEM_NONE
	|| sm_SemUnwedge(nodeListMutex, 3) < 0 || sm_SemTake(nodeListMutex) < 0)
	{
		putErrmsg("Can't lock node list file.", NULL);
		return -1;
	}

	isprintf(nodeListFileName, sizeof nodeListFileName, "%.255s%cion_nodes",
			nodeListDir, ION_PATH_DELIMITER);
	if (nodeNbr == 0)	/*	Just attaching.			*/
	{
		nodeListFile = open(nodeListFileName, O_RDONLY, 0);
	}
	else			/*	Initializing the node.		*/
	{
		nodeListFile = open(nodeListFileName, O_RDWR | O_CREAT, 0666);
	}

	if (nodeListFile < 0)
	{
		sm_SemGive(nodeListMutex);
		putSysErrmsg("Can't open ion_nodes file", nodeListFileName);
		writeMemo("[?] Remove ION_NODE_LIST_DIR from env?");
		return -1;
	}

	while (1)
	{
		if (igets(nodeListFile, lineBuf, sizeof lineBuf, &lineLen)
				== NULL)
		{
			if (lineLen < 0)
			{
				close(nodeListFile);
				sm_SemGive(nodeListMutex);
				putErrmsg("Failed reading ion_nodes file.",
						nodeListFileName);
				return -1;
			}

			break;		/*	End of file.		*/
		}

		lineNbr++;
		if (sscanf(lineBuf, UVAST_FIELDSPEC " %d %31s %255s",
			&lineNodeNbr, &lineWmKey, lineSdrName, lineWdName) < 4)
		{
			close(nodeListFile);
			sm_SemGive(nodeListMutex);
			putErrmsg("Syntax error at line#", itoa(lineNbr));
			writeMemoNote("[?] Repair ion_nodes file.",
					nodeListFileName);
			return -1;
		}

		if (lineNodeNbr == nodeNbr)		/*	Match.	*/
		{
			/*	lineNodeNbr can't be zero (we never
			 *	write such lines to the file), so this
			 *	must be matching non-zero node numbers.
			 *	So we are re-initializing this node.	*/

			close(nodeListFile);
			if (strcmp(lineWdName, wdName) != 0)
			{
				sm_SemGive(nodeListMutex);
				putErrmsg("CWD conflict at line#",
						itoa(lineNbr));
				writeMemoNote("[?] Repair ion_nodes file.",
						nodeListFileName);
				return -1;
			}

			if (parms->wmKey == 0)
			{
				parms->wmKey = lineWmKey;
			}

			if (parms->wmKey != lineWmKey)
			{
				sm_SemGive(nodeListMutex);
				putErrmsg("WmKey conflict at line#",
						itoa(lineNbr));
				writeMemoNote("[?] Repair ion_nodes file.",
						nodeListFileName);
				return -1;
			}

			if (parms->sdrName[0] == '\0')
			{
				istrcpy(parms->sdrName, lineSdrName,
						sizeof parms->sdrName);
			}

			if (strcmp(parms->sdrName, lineSdrName) != 0)
			{
				sm_SemGive(nodeListMutex);
				putErrmsg("SdrName conflict at line#",
						itoa(lineNbr));
				writeMemoNote("[?] Repair ion_nodes file.",
						nodeListFileName);
				return -1;
			}

			return 0;
		}

		/*	lineNodeNbr does not match nodeNbr (which may
		 *	be zero).					*/

		if (strcmp(lineWdName, wdName) == 0)	/*	Match.	*/
		{
			close(nodeListFile);
			sm_SemGive(nodeListMutex);
			if (nodeNbr == 0)	/*	Attaching.	*/
			{
				parms->wmKey = lineWmKey;
				istrcpy(parms->sdrName, lineSdrName,
						MAX_SDR_NAME + 1);
				return 0;
			}

			/*	Reinitialization conflict.		*/

			putErrmsg("NodeNbr conflict at line#", itoa(lineNbr));
			writeMemoNote("[?] Repair ion_nodes file.",
					nodeListFileName);
			return -1;
		}

		/*	Haven't found matching line yet.  Continue.	*/
	}

	/*	No matching lines in file.				*/

	if (nodeNbr == 0)	/*	Attaching to existing node.	*/
	{
		close(nodeListFile);
		sm_SemGive(nodeListMutex);
		putErrmsg("No node has been initialized in this directory.",
				wdName);
		return -1;
	}

	/*	Initializing, so append line to the nodes list file.	*/

	if (parms->wmKey == 0)
	{
		parms->wmKey = ION_DEFAULT_SM_KEY;
	}

	if (parms->sdrName[0] == '\0')
	{
		istrcpy(parms->sdrName, ION_DEFAULT_SDR_NAME,
				sizeof parms->sdrName);
	}

	isprintf(lineBuf, sizeof lineBuf, UVAST_FIELDSPEC " %d %.31s %.255s\n",
			nodeNbr, parms->wmKey, parms->sdrName, wdName);
	result = iputs(nodeListFile, lineBuf);
	close(nodeListFile);
	sm_SemGive(nodeListMutex);
	if (result < 0)
	{
		putErrmsg("Failed writing to ion_nodes file.", NULL);
		return -1;
	}

	return 0;
}

int	ionAttach()
{
	Sdr		ionsdr = _ionsdr(NULL);
	Object		iondbObject = _iondbObject(NULL);
	PsmPartition	ionwm = _ionwm(NULL);
	IonVdb		*ionvdb = _ionvdb(NULL);
	char		*wdname;
	char		wdnamebuf[256];
	IonParms	parms;
	sm_WmParms	ionwmParms;
	char		*ionvdbName = "ionvdb";
	ZcoCallback	notify = ionProvideZcoSpace;

	if (ionsdr && iondbObject && ionwm && ionvdb)
	{
		return 0;	/*	Already attached.		*/
	}

	if (sdr_initialize(0, NULL, SM_NO_KEY, NULL) < 0)
	{
		putErrmsg("Can't initialize the SDR system.", NULL);
		return -1;
	}

	wdname = getenv("ION_NODE_WDNAME");
	if (wdname == NULL)
	{
		if (igetcwd(wdnamebuf, 256) == NULL)
		{
			putErrmsg("Can't get cwd name.", NULL);
			return -1;
		}

		wdname = wdnamebuf;
	}

	memset((char *) &parms, 0, sizeof parms);
	if (checkNodeListParms(&parms, wdname, 0) < 0)
	{
		putErrmsg("Failed checking node list parms.", NULL);
		return -1;
	}

	if (ionsdr == NULL)
	{
		ionsdr = sdr_start_using(parms.sdrName);
		if (ionsdr == NULL)
		{
			putErrmsg("Can't start using SDR for ION.", NULL);
			return -1;
		}

		oK(_ionsdr(&ionsdr));
	}

	if (iondbObject == 0)
	{
		if (sdr_heap_is_halted(ionsdr))
		{
			iondbObject = sdr_find(ionsdr, "iondb", NULL);
		}
		else
		{
			CHKERR(sdr_begin_xn(ionsdr));
			iondbObject = sdr_find(ionsdr, "iondb", NULL);
			sdr_exit_xn(ionsdr);
		}

		if (iondbObject == 0)
		{
			putErrmsg("ION database not found.", NULL);
			return -1;
		}

		oK(_iondbObject(&iondbObject));
	}

	oK(_ionConstants());

	/*	Open ION shared-memory partition.			*/

	if (ionwm == NULL)
	{
		ionwmParms.wmKey = parms.wmKey;
		ionwmParms.wmSize = 0;
		ionwmParms.wmAddress = NULL;
		ionwmParms.wmName = ION_SM_NAME;
		ionwm = _ionwm(&ionwmParms);
		if (ionwm == NULL)
		{
			putErrmsg("Can't open access to ION memory.", NULL);
			return -1;
		}
	}

	if (ionvdb == NULL)
	{
		if (_ionvdb(&ionvdbName) == NULL)
		{
			putErrmsg("ION volatile database not found.", NULL);
			return -1;
		}
	}

	zco_register_callback(notify);
	ionRedirectMemos();
	ionRedirectWatchCharacters();

	return 0;
}

void	ionDetach()
{
	Sdr	ionsdr = _ionsdr(NULL);

	if (ionsdr)
	{
		sdr_stop_using(ionsdr);
		ionsdr = NULL;		/*	To reset to NULL.	*/
		oK(_ionsdr(&ionsdr));
	}
}

void	ionProd(uvast fromNode, uvast toNode, unsigned int xmitRate,
		unsigned int owlt)
{
	Sdr		ionsdr = _ionsdr(NULL);
	time_t		fromTime;
	time_t		toTime;
	char		textbuf[RFX_NOTE_LEN];
	PsmAddress	xaddr;

	if (ionsdr == NULL)
	{
		if (ionAttach() < 0)
		{
			writeMemo("[?] ionProd: node not initialized yet.");
			return;
		}
	}

	fromTime = getUTCTime();	/*	The current time.	*/
	toTime = fromTime + 14400;	/*	Four hours later.	*/
	if (rfx_insert_range(fromTime, toTime, fromNode, toNode, owlt,
			&xaddr) < 0 || xaddr == 0)
	{
		writeMemoNote("[?] ionProd: range insertion failed.",
				utoa(owlt));
		return;
	}

	writeMemo("ionProd: range inserted.");
	writeMemo(rfx_print_range(xaddr, textbuf));
	if (rfx_insert_contact(fromTime, toTime, fromNode, toNode, xmitRate,
			1.0, &xaddr) < 0 || xaddr == 0)
	{
		writeMemoNote("[?] ionProd: contact insertion failed.",
				utoa(xmitRate));
		return;
	}

	writeMemo("ionProd: contact inserted.");
	writeMemo(rfx_print_contact(xaddr, textbuf));
}

Sdr	getIonsdr()
{
	return _ionsdr(NULL);
}

Object	getIonDbObject()
{
	return _iondbObject(NULL);
}

PsmPartition	getIonwm()
{
	return _ionwm(NULL);
}

int	getIonMemoryMgr()
{
	return _ionMemory(NULL);
}

IonVdb	*getIonVdb()
{
	return _ionvdb(NULL);
}

char	*getIonWorkingDirectory()
{
	IonDB	*snapshot = _ionConstants();

	if (snapshot == NULL)
	{
		return ".";
	}

	return snapshot->workingDirectoryName;
}

uvast	getOwnNodeNbr()
{
	IonDB	*snapshot = _ionConstants();

	if (snapshot == NULL)
	{
		return 0;
	}

	return snapshot->ownNodeNbr;
}

int	ionClockIsSynchronized()
{
	Sdr	ionsdr = _ionsdr(NULL);
	Object	iondbObject = _iondbObject(NULL);
	IonDB	iondbBuf;

	sdr_read(ionsdr, (char *) &iondbBuf, iondbObject, sizeof(IonDB));
	return iondbBuf.clockIsSynchronized;
}


/*	*	*	Timestamp handling 	*	*	*	*/

time_t	getUTCTime()
{
	IonVdb	*ionvdb = _ionvdb(NULL);
	int	delta = ionvdb ? ionvdb->deltaFromUTC : 0;
	time_t	clocktime;

	clocktime = time(NULL);

	return clocktime - delta;
}


void	writeTimestampLocal(time_t timestamp, char *timestampBuffer)
{
	struct tm	tsbuf;
	struct tm	*ts = &tsbuf;

	CHKVOID(timestampBuffer);
	oK(localtime_r(&timestamp, &tsbuf));

	isprintf(timestampBuffer, 20, timestampOutFormat,
			ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday,
			ts->tm_hour, ts->tm_min, ts->tm_sec);
}

void	writeTimestampUTC(time_t timestamp, char *timestampBuffer)
{
	struct tm	tsbuf;
	struct tm	*ts = &tsbuf;

	CHKVOID(timestampBuffer);
	oK(gmtime_r(&timestamp, &tsbuf));

	isprintf(timestampBuffer, 20, timestampOutFormat,
			ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday,
			ts->tm_hour, ts->tm_min, ts->tm_sec);
}

/*	*	*	Parsing 	*	*	*	*	*/

int	_extractSdnv(uvast *into, unsigned char **from, int *remnant,
		int lineNbr)
{
	int	sdnvLength;

	CHKZERO(into && from && remnant);
	if (*remnant < 1)
	{
		writeMemoNote("[?] Missing SDNV at line...", itoa(lineNbr));
		return 0;
	}

	sdnvLength = decodeSdnv(into, *from);
	if (sdnvLength < 1)
	{
		writeMemoNote("[?] Invalid SDNV at line...", itoa(lineNbr));
		return 0;
	}

	(*from) += sdnvLength;
	(*remnant) -= sdnvLength;
	return sdnvLength;
}

int	_extractSmallSdnv(unsigned int *into, unsigned char **from,
		int *remnant, int lineNbr)
{
	int	sdnvLength;
	uvast	val;

	CHKZERO(into && from && remnant);
	if (*remnant < 1)
	{
		writeMemoNote("[?] Missing SDNV at line...", itoa(lineNbr));
		return 0;
	}

	sdnvLength = decodeSdnv(&val, *from);
	if (sdnvLength < 1)
	{
		writeMemoNote("[?] Invalid SDNV at line...", itoa(lineNbr));
		return 0;
	}

	*into = val;				/*	Truncate.	*/
	(*from) += sdnvLength;
	(*remnant) -= sdnvLength;
	return sdnvLength;
}

/*	*	*	Debugging 	*	*	*	*	*/

int	ionLocked()
{
	return sdr_in_xn(_ionsdr(NULL));	/*	Boolean.	*/
}

/*	Functions for signaling the main threads of processes.	*	*/

#define	PROC_NAME_LEN	16
#define	MAX_PROCS	16

typedef struct
{
	char		procName[PROC_NAME_LEN];
	pthread_t	mainThread;
} IonProc;

static pthread_t	_mainThread(char *procName)
{
	static IonProc	proc[MAX_PROCS + 1];
	static int	procCount = 0;
	int		i;

	for (i = 0; i < procCount; i++)
	{
		if (strcmp(proc[i].procName, procName) == 0)
		{
			break;
		}
	}

	if (i == procCount)	/*	Registering new process.	*/
	{
		if (procCount == MAX_PROCS)
		{
			/*	Can't register process; return an
			 *	invalid value for mainThread.		*/

			return proc[MAX_PROCS].mainThread;
		}

		/*	Initial call to _mainThread for any process
		 *	must be from the main thread of that process.	*/

		procCount++;
		istrcpy(proc[i].procName, procName, PROC_NAME_LEN);
		proc[i].mainThread = pthread_self();
	}

	return proc[i].mainThread;
}

void	ionNoteMainThread(char *procName)
{
	CHKVOID(procName);
	oK(_mainThread(procName));
}

void	ionPauseMainThread(int seconds)
{
	if (seconds < 0)
	{
		seconds = 2000000000;
	}

	snooze(seconds);
}

void	ionKillMainThread(char *procName)
{
	pthread_t	mainThread;

	CHKVOID(procName);
       	mainThread = _mainThread(procName);
	if (!pthread_equal(mainThread, pthread_self()))
	{
		pthread_kill(mainThread, SIGTERM);
	}
}

/*	Functions for flow-controlled ZCO space management.		*/

void	ionShred(ReqTicket ticket)
{
	Sdr		sdr = getIonsdr();
	PsmPartition	ionwm = getIonwm();

	/*	Ticket is address of an sm_list element in a shared
	 *	memory list of requisitions in the IonVdb.		*/

	CHKVOID(ticket);
	CHKVOID(sdr_begin_xn(sdr));	/*	Must be atomic.		*/
	psm_free(ionwm, sm_list_data(ionwm, ticket));
	sm_list_delete(ionwm, ticket, NULL, NULL);
	sdr_exit_xn(sdr);	/*	End of critical section.	*/
}

int	ionRequestZcoSpace(ZcoAcct acct, vast fileSpaceNeeded,
			vast bulkSpaceNeeded, vast heapSpaceNeeded,
			unsigned char coarsePriority,
			unsigned char finePriority,
			ReqAttendant *attendant, ReqTicket *ticket)
{
	Sdr		sdr = getIonsdr();
	PsmPartition	ionwm = getIonwm();
	IonVdb		*vdb = getIonVdb();
	PsmAddress	reqAddr;
	Requisition	*req;
	PsmAddress	elt;
	PsmAddress	oldReqAddr;
	Requisition	*oldReq;

	CHKERR(acct == ZcoInbound || acct == ZcoOutbound);
	CHKERR(fileSpaceNeeded >= 0);
	CHKERR(bulkSpaceNeeded >= 0);
	CHKERR(heapSpaceNeeded >= 0);
	CHKERR(ticket);
	CHKERR(vdb);
	*ticket = 0;			/*	Default: serviced.	*/
	oK(sdr_begin_xn(sdr));		/*	Just to lock memory.	*/
	reqAddr = psm_zalloc(ionwm, sizeof(Requisition));
	if (reqAddr == 0)
	{
		sdr_exit_xn(sdr);
		putErrmsg("Can't create ZCO space requisition.", NULL);
		return -1;
	}

	req = (Requisition *) psp(ionwm, reqAddr);
	req->fileSpaceNeeded = fileSpaceNeeded;
	req->bulkSpaceNeeded = bulkSpaceNeeded;
	req->heapSpaceNeeded = heapSpaceNeeded;
	if (attendant)
	{
		req->semaphore = attendant->semaphore;
	}
	else
	{
		req->semaphore = SM_SEM_NONE;
	}

	req->secondsUnclaimed = -1;	/*	Not yet serviced.	*/
	req->coarsePriority = coarsePriority;
	req->finePriority = finePriority;
	for (elt = sm_list_last(ionwm, vdb->requisitions[acct]); elt;
			elt = sm_list_prev(ionwm, elt))
	{
		oldReqAddr = sm_list_data(ionwm, elt);
		oldReq = (Requisition *) psp(ionwm, oldReqAddr);
		if (oldReq->coarsePriority > req->coarsePriority)
		{
			break;		/*	Insert after this one.	*/
		}

		if (oldReq->coarsePriority < req->coarsePriority)
		{
			continue;	/*	Move toward the start.	*/
		}

		/*	Same coarse priority.				*/

		if (oldReq->finePriority > req->finePriority)
		{
			break;		/*	Insert after this one.	*/
		}

		if (oldReq->finePriority < req->finePriority)
		{
			continue;	/*	Move toward the start.	*/
		}

		/*	Same priority, so FIFO; insert after this one.	*/

		break;
	}

	if (elt)
	{
		*ticket = sm_list_insert_after(ionwm, elt, reqAddr);
	}
	else	/*	Higher priority than all other requisitions.	*/
	{
		*ticket = sm_list_insert_first(ionwm,
				vdb->requisitions[acct], reqAddr);
	}

	if (*ticket == 0)
	{
		psm_free(ionwm, reqAddr);
		sdr_exit_xn(sdr);
		putErrmsg("Can't put ZCO space requisition into list.", NULL);
		return -1;
	}

	sdr_exit_xn(sdr);		/*	Unlock memory.		*/

	/*	See if request can be serviced immediately.		*/

	ionProvideZcoSpace(acct);
	if (req->secondsUnclaimed >= 0)	/*	Got it!			*/
	{
		ionShred(*ticket);
		*ticket = 0;		/*	Nothing to wait for.	*/
		return 0;
	}

	/*	Request can't be serviced yet.				*/

	if (attendant)
	{
		/*	Get attendant ready to wait for service.	*/

		sm_SemGive(attendant->semaphore);	/*	Unlock.	*/
		sm_SemTake(attendant->semaphore);	/*	Lock.	*/
	}

	return 0;
}

static void	ionProvideZcoSpace(ZcoAcct acct)
{
	Sdr		sdr = getIonsdr();
	PsmPartition	ionwm = getIonwm();
	IonVdb		*vdb = getIonVdb();
	vast		maxFileOccupancy;
	vast		maxBulkOccupancy;
	vast		maxHeapOccupancy;
	vast		currentFileOccupancy;
	vast		currentBulkOccupancy;
	vast		currentHeapOccupancy;
	vast		totalFileSpaceAvbl;
	vast		totalBulkSpaceAvbl;
	vast		totalHeapSpaceAvbl;
	vast		restrictedFileSpaceAvbl;
	vast		restrictedBulkSpaceAvbl;
	vast		restrictedHeapSpaceAvbl;
	vast		fileSpaceAvbl;
	vast		bulkSpaceAvbl;
	vast		heapSpaceAvbl;
	PsmAddress	elt;
	PsmAddress	reqAddr;
	Requisition	*req;

	CHKVOID(vdb);
	oK(sdr_begin_xn(sdr));		/*	Just to lock memory.	*/
	maxFileOccupancy = zco_get_max_file_occupancy(sdr, acct);
	maxBulkOccupancy = zco_get_max_bulk_occupancy(sdr, acct);
	maxHeapOccupancy = zco_get_max_heap_occupancy(sdr, acct);
	currentFileOccupancy = zco_get_file_occupancy(sdr, acct);
	currentBulkOccupancy = zco_get_bulk_occupancy(sdr, acct);
	currentHeapOccupancy = zco_get_heap_occupancy(sdr, acct);
	totalFileSpaceAvbl = maxFileOccupancy - currentFileOccupancy;
	totalBulkSpaceAvbl = maxBulkOccupancy - currentBulkOccupancy;
	totalHeapSpaceAvbl = maxHeapOccupancy - currentHeapOccupancy;

	/*	Requestors that are willing to wait for space are not
	 *	allowed to fill up all available space; for these
	 *	requestors, maximum occupancy is reduced by 1/2.  This
	 *	is to ensure that these requestors cannot prevent
	 *	allocation of ZCO space to requestors that cannot
	 *	wait for it.						*/

	restrictedFileSpaceAvbl = (maxFileOccupancy / 2) - currentFileOccupancy;
	restrictedBulkSpaceAvbl = (maxBulkOccupancy / 2) - currentBulkOccupancy;
	restrictedHeapSpaceAvbl = (maxHeapOccupancy / 2) - currentHeapOccupancy;
	for (elt = sm_list_first(ionwm, vdb->requisitions[acct]); elt;
			elt = sm_list_next(ionwm, elt))
	{
		reqAddr = sm_list_data(ionwm, elt);
		req = (Requisition *) psp(ionwm, reqAddr);
		if (req->secondsUnclaimed >= 0)
		{
			/*	This request has already been serviced.
			 *	The requested space has been reserved
			 *	for it, so that space is not available
			 *	for any other requests.			*/

			totalFileSpaceAvbl -= req->fileSpaceNeeded;
			totalBulkSpaceAvbl -= req->bulkSpaceNeeded;
			totalHeapSpaceAvbl -= req->heapSpaceNeeded;
			restrictedFileSpaceAvbl -= req->fileSpaceNeeded;
			restrictedBulkSpaceAvbl -= req->bulkSpaceNeeded;
			restrictedHeapSpaceAvbl -= req->heapSpaceNeeded;
			continue;	/*	Req already serviced.	*/
		}

		if (req->semaphore == SM_SEM_NONE)
		{
			fileSpaceAvbl = totalFileSpaceAvbl;
			bulkSpaceAvbl = totalBulkSpaceAvbl;
			heapSpaceAvbl = totalHeapSpaceAvbl;
		}
		else
		{
			fileSpaceAvbl = restrictedFileSpaceAvbl;
			bulkSpaceAvbl = restrictedBulkSpaceAvbl;
			heapSpaceAvbl = restrictedHeapSpaceAvbl;
		}

		if (fileSpaceAvbl < 0)
		{
			fileSpaceAvbl = 0;
		}

		if (bulkSpaceAvbl < 0)
		{
			bulkSpaceAvbl = 0;
		}

		if (heapSpaceAvbl < 0)
		{
			heapSpaceAvbl = 0;
		}

		if (fileSpaceAvbl < req->fileSpaceNeeded
		|| bulkSpaceAvbl < req->bulkSpaceNeeded
		|| heapSpaceAvbl < req->heapSpaceNeeded)
		{
			/*	Can't provide ZCO space to this
			 *	requisition at this time.		*/

			continue;
		}

		/*	Can service this requisition.			*/

		req->secondsUnclaimed = 0;
		if (req->semaphore != SM_SEM_NONE)
		{
			sm_SemGive(req->semaphore);
		}

		totalFileSpaceAvbl -= req->fileSpaceNeeded;
		totalBulkSpaceAvbl -= req->bulkSpaceNeeded;
		totalHeapSpaceAvbl -= req->heapSpaceNeeded;
		restrictedFileSpaceAvbl -= req->fileSpaceNeeded;
		restrictedBulkSpaceAvbl -= req->bulkSpaceNeeded;
		restrictedHeapSpaceAvbl -= req->heapSpaceNeeded;
	}

	sdr_exit_xn(sdr);		/*	Unlock memory.		*/
}

Object	ionCreateZco(ZcoMedium source, Object location, vast offset,
		vast length, unsigned char coarsePriority,
		unsigned char finePriority, ZcoAcct acct,
		ReqAttendant *attendant)
{
	Sdr		sdr = getIonsdr();
	IonVdb		*vdb = getIonVdb();
	unsigned char	provisional;
	vast		fileSpaceNeeded = 0;
	vast		bulkSpaceNeeded = 0;
	vast		heapSpaceNeeded = 0;
	ReqTicket	ticket;
	Object		zco;

	CHKERR(vdb);
	CHKERR(acct == ZcoInbound || acct == ZcoOutbound);
	provisional = (acct == ZcoInbound && attendant == NULL ? 1 : 0);
	if (location == 0)	/*	No initial extent to write.	*/
	{
		oK(sdr_begin_xn(sdr));
		zco = zco_create(sdr, source, 0, 0, 0, acct, provisional);
		if (sdr_end_xn(sdr) < 0 || zco == (Object) ERROR)
		{
			putErrmsg("Can't create ZCO.", NULL);
			return ((Object) ERROR);
		}

		return zco;
	}

	CHKERR(offset >= 0);
	CHKERR(length > 0);

	/*	Creating ZCO with its initial extent.			*/

	switch (source)
	{
	case ZcoFileSource:
		fileSpaceNeeded = length;
		break;

	case ZcoBulkSource:
		bulkSpaceNeeded = length;
		break;

	case ZcoSdrSource:
		heapSpaceNeeded = length;
		break;

	case ZcoZcoSource:
		oK(sdr_begin_xn(sdr));
		zco_get_aggregate_length(sdr, location, offset, length,
			&fileSpaceNeeded, &bulkSpaceNeeded, &heapSpaceNeeded);
		sdr_exit_xn(sdr);
		break;

	default:
		putErrmsg("Invalid ZCO source type.", itoa((int) source));
		return ((Object) ERROR);
	}

	if (ionRequestZcoSpace(acct, fileSpaceNeeded, bulkSpaceNeeded,
			heapSpaceNeeded, coarsePriority, finePriority,
			attendant, &ticket) < 0)
	{
		putErrmsg("Failed on ionRequest.", NULL);
		return ((Object) ERROR);
	}

	if (ticket)	/*	Couldn't service request immediately.	*/
	{
		if (attendant == NULL)	/*	Non-blocking.		*/
		{
			ionShred(ticket);
			return 0;	/*	No Zco created.		*/
		}

		/*	Ticket is req list element for the request.	*/

		if (sm_SemTake(attendant->semaphore) < 0)
		{
			putErrmsg("ionCreateZco can't take semaphore.", NULL);
			ionShred(ticket);
			return ((Object) ERROR);
		}

		if (sm_SemEnded(attendant->semaphore))
		{
			writeMemo("[i] ZCO creation interrupted.");
			ionShred(ticket);
			return 0;
		}

		/*	Request has been serviced; can now create ZCO.	*/

		ionShred(ticket);
	}

	/*	Pass additive inverse of length to zco_create to
 	*	indicate that space has already been awarded.		*/

	oK(sdr_begin_xn(sdr));
	zco = zco_create(sdr, source, location, offset, 0 - length, acct,
			provisional);
	if (sdr_end_xn(sdr) < 0 || zco == (Object) ERROR || zco == 0)
	{
		putErrmsg("Can't create ZCO.", NULL);
		return ((Object) ERROR);
	}

	return zco;
}
