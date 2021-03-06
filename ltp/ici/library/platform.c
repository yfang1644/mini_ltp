/*
	platform.c:	platform-dependent implementation of common
			functions, to simplify porting.
									*/
/*	Copyright (c) 1997, California Institute of Technology.		*/
/*	ALL RIGHTS RESERVED. U.S. Government Sponsorship		*/
/*	acknowledged.							*/
/*									*/
/*	Author: Scott Burleigh, Jet Propulsion Laboratory		*/
/*									*/
/*	Scalar/SDNV conversion functions written by			*/
/*	Ioannis Alexiadis, Democritus University of Thrace, 2011.	*/
/*									*/
#include "platform.h"

#define	ABORT_AS_REQD		if (_coreFileNeeded(NULL)) sm_Abort()

typedef struct rlock_str
{
	pthread_mutex_t	semaphore;
	pthread_t	owner;
	short		count;
	unsigned char	init;		/*	Boolean.		*/
	unsigned char	owned;		/*	Boolean.		*/
} Rlock;		/*	Private-memory semaphore.		*/ 

int	initResourceLock(ResourceLock *rl)
{
	Rlock	*lock = (Rlock *) rl;

	if (lock == NULL)
	{
		ABORT_AS_REQD;
		return ERROR;
	}

	if (lock->init)
	{
		return 0;
	}

	memset((char *) lock, 0, sizeof(Rlock));
	if (pthread_mutex_init(&(lock->semaphore), NULL))
	{
		writeErrMemo("Can't create lock semaphore");
		return -1;
	}

	lock->init = 1;
	return 0;
}

void	killResourceLock(ResourceLock *rl)
{
	Rlock	*lock = (Rlock *) rl;

	if (lock && lock->init && lock->count == 0)
	{
		oK(pthread_mutex_destroy(&(lock->semaphore)));
		lock->init = 0;
	}
}

void	lockResource(ResourceLock *rl)
{
	Rlock		*lock = (Rlock *) rl;
	pthread_t	tid;

	if (lock && lock->init)
	{
		tid = pthread_self();
		if (lock->owned == 0 || !pthread_equal(tid, lock->owner))
		{
			oK(pthread_mutex_lock(&(lock->semaphore)));
			lock->owner = tid;
			lock->owned = 1;
		}

		(lock->count)++;
	}
}

void	unlockResource(ResourceLock *rl)
{
	Rlock		*lock = (Rlock *) rl;
	pthread_t	tid;

	if (lock && lock->init)
	{
		tid = pthread_self();
		if (lock->owned && pthread_equal(tid, lock->owner))
		{
			(lock->count)--;
			if ((lock->count) == 0)
			{
				lock->owned = 0;
				oK(pthread_mutex_unlock(&(lock->semaphore)));
			}
		}
	}
}

void	snooze(unsigned int seconds)
{
	struct timespec	ts;

	ts.tv_sec = seconds;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
}

void	microsnooze(unsigned int usec)
{
	struct timespec	ts;

	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (usec % 1000000) * 1000;
	nanosleep(&ts, NULL);
}

void	getCurrentTime(struct timeval *tvp)
{
	CHKVOID(tvp);
	gettimeofday(tvp, NULL);
}

char	*system_error_msg()
{
	return strerror(errno);
}

char	*getNameOfUser(char *buffer)
{
	CHKNULL(buffer);
	uid_t		euid;
	struct passwd	*pwd;

	/*	Note: buffer is in argument list for portability but
	 *	is not used and therefore is not checked for non-NULL.	*/

	euid = geteuid();
	pwd = getpwuid(euid);
	if (pwd)
	{
		return pwd->pw_name;
	}

	return "";
}

unsigned int	getInternetAddress(char *hostName)
{
	struct hostent	*hostInfo;
	unsigned int	hostInetAddress;

	CHKZERO(hostName);
	hostInfo = gethostbyname(hostName);
	if (hostInfo == NULL)
	{
		putSysErrmsg("can't get host info", hostName);
		return BAD_HOST_NAME;
	}

	if (hostInfo->h_length != sizeof hostInetAddress)
	{
		putErrmsg("Address length invalid in host info.", hostName);
		return BAD_HOST_NAME;
	}

	memcpy((char *) &hostInetAddress, hostInfo->h_addr, 4);
	return ntohl(hostInetAddress);
}

char	*getInternetHostName(unsigned int hostNbr, char *buffer)
{
	struct hostent	*hostInfo;

	CHKNULL(buffer);
	hostNbr = htonl(hostNbr);
	hostInfo = gethostbyaddr((char *) &hostNbr, sizeof hostNbr, AF_INET);
	if (hostInfo == NULL)
	{
		putSysErrmsg("can't get host info", utoa(hostNbr));
		return NULL;
	}

	strncpy(buffer, hostInfo->h_name, MAXHOSTNAMELEN);
	return buffer;
}

int	getNameOfHost(char *buffer, int bufferLength)
{
	int	result;

	CHKERR(buffer);
	CHKERR(bufferLength > 0);
	result = gethostname(buffer, bufferLength);
	if (result < 0)
	{
		putSysErrmsg("can't local host name", NULL);
	}

	return result;
}

int	reUseAddress(int fd)
{
	int	result;
	int	i = 1;

	result = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &i,
			sizeof i);
#if (defined (SO_REUSEPORT))
	result += setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char *) &i,
			sizeof i);
#endif
	if (result < 0)
	{
		putSysErrmsg("can't make socket address reusable", NULL);
	}

	return result;
}
 
/******************* platform-independent functions *********************/

void	*acquireSystemMemory(size_t size)
{
	void	*block;

	if (size <= 0)
	{
		return NULL;
	}

	size = size + ((sizeof(void *)) - (size % (sizeof(void *))));
#if defined (RTEMS)
	block = malloc(size);	/*	try posix_memalign?		*/
#else
	block = memalign((size_t) (sizeof(void *)), size);
#endif
	if (block)
	{
		TRACK_MALLOC(block);
		memset((char *) block, 0, size);
	}
	else
	{
		putSysErrmsg("Memory allocation failed", itoa(size));
	}

	return block;
}

static void	watchToStdout(char token)
{
	putchar(token);
	fflush(stdout);
}

static Watcher	_watchOneEvent(Watcher *watchFunction)
{
	static Watcher	watcher = watchToStdout;

	if (watchFunction)
	{
		watcher = *watchFunction;
	}

	return watcher;
}

void	setWatcher(Watcher watchFunction)
{
	if (watchFunction)
	{
		oK(_watchOneEvent(&watchFunction));
	}
}

void	iwatch(char token)
{
	(_watchOneEvent(NULL))(token);
}

static void	logToStdout(char *text)
{
	if (text)
	{
		fprintf(stdout, "%s\n", text);
		fflush(stdout);
	}
}

static Logger	_logOneMessage(Logger *logFunction)
{
	static Logger	logger = logToStdout;

	if (logFunction)
	{
		logger = *logFunction;
	}

	return logger;
}

void	setLogger(Logger logFunction)
{
	if (logFunction)
	{
		oK(_logOneMessage(&logFunction));
	}
}

void	writeMemo(char *text)
{
	if (text)
	{
		(_logOneMessage(NULL))(text);
	}
}

void	writeMemoNote(char *text, char *note)
{
	char	*noteText = note ? note : "";
	char	textBuffer[1024];

	if (text)
	{
		isprintf(textBuffer, sizeof textBuffer, "%.900s: %.64s",
				text, noteText);
		(_logOneMessage(NULL))(textBuffer);
	}
}

void	writeErrMemo(char *text)
{
	writeMemoNote(text, system_error_msg());
}

char	*iToa(int arg)
{
	static char	itoa_str[33];

	isprintf(itoa_str, sizeof itoa_str, "%d", arg);
	return itoa_str;
}

char	*uToa(unsigned int arg)
{
	static char	utoa_str[33];

	isprintf(utoa_str, sizeof utoa_str, "%u", arg);
	return utoa_str;
}

static int	clipFileName(const char *qualifiedFileName, char **fileName)
{
	int	fileNameLength;
	int	excessLength;

	fileNameLength = strlen(qualifiedFileName);
	excessLength = fileNameLength - MAX_SRC_FILE_NAME;
	if (excessLength < 0)
	{
		excessLength = 0;
	}

	/*	Clip excessLength bytes off the front of the file
	 *	name by adding excessLength to the string pointer.	*/

	(*fileName) = ((char *) qualifiedFileName) + excessLength;
	fileNameLength -= excessLength;
	return fileNameLength;
}

static int	_errmsgs(int lineNbr, const char *qualifiedFileName,
			const char *text, const char *arg, char *buffer)
{
	static char		errmsgs[ERRMSGS_BUFSIZE];
	static int		errmsgsLength = 0;
	static ResourceLock	errmsgsLock;
	static int		errmsgsLockInit = 0;
	int			msgLength;
	int			spaceFreed;
	int			fileNameLength;
	char			*fileName;
	char			lineNbrBuffer[32];
	int			spaceAvbl;
	int			spaceForText;
	int			spaceNeeded;

	if (!errmsgsLockInit)
	{
		memset((char *) &errmsgsLock, 0, sizeof(ResourceLock));
		if (initResourceLock(&errmsgsLock) < 0)
		{
			ABORT_AS_REQD;
			return 0;
		}

		errmsgsLockInit = 1;
	}

	if (buffer)		/*	Retrieving an errmsg.		*/
	{
		if (errmsgsLength == 0)	/*	No more msgs in pool.	*/
		{
			return 0;
		}

		lockResource(&errmsgsLock);
		msgLength = strlen(errmsgs);
		if (msgLength == 0)	/*	No more msgs in pool.	*/
		{
			unlockResource(&errmsgsLock);
			return msgLength;
		}

		/*	Getting a message removes it from the pool,
		 *	releasing space for more messages.		*/

		spaceFreed = msgLength + 1;	/*	incl. last NULL	*/
		memcpy(buffer, errmsgs, spaceFreed);
		errmsgsLength -= spaceFreed;
		memcpy(errmsgs, errmsgs + spaceFreed, errmsgsLength);
		memset(errmsgs + errmsgsLength, 0, spaceFreed);
		unlockResource(&errmsgsLock);
		return msgLength;
	}

	/*	Posting an errmsg.					*/

	if (qualifiedFileName == NULL || text == NULL || *text == '\0')
	{
		return 0;	/*	Ignored.			*/
	}

	fileNameLength = clipFileName(qualifiedFileName, &fileName);
	lockResource(&errmsgsLock);
	isprintf(lineNbrBuffer, sizeof lineNbrBuffer, "%d", lineNbr);
	spaceAvbl = ERRMSGS_BUFSIZE - errmsgsLength;
	spaceForText = 8 + strlen(lineNbrBuffer) + 4 + fileNameLength
			+ 2 + strlen(text);
	spaceNeeded = spaceForText + 1;
	if (arg)
	{
		spaceNeeded += (2 + strlen(arg) + 1);
	}

	if (spaceNeeded > spaceAvbl)	/*	Can't record message.	*/
	{
		if (spaceAvbl < 2)
		{
			/*	Can't even note that it was omitted.	*/

			spaceNeeded = 0;
		}
		else
		{
			/*	Write a single newline message to
			 *	note that this message was omitted.	*/

			spaceNeeded = 2;
			errmsgs[errmsgsLength] = '\n';
			errmsgs[errmsgsLength + 1] = '\0';
		}
	}
	else
	{
		isprintf(errmsgs + errmsgsLength, spaceAvbl,
			"at line %s of %s, %s", lineNbrBuffer, fileName, text);
		if (arg)
		{
			isprintf(errmsgs + errmsgsLength + spaceForText,
				spaceAvbl - spaceForText, " (%s)", arg);
		}
	}

	errmsgsLength += spaceNeeded;
	unlockResource(&errmsgsLock);
	return 0;
}

void	_postErrmsg(const char *fileName, int lineNbr, const char *text,
		const char *arg)
{
	oK(_errmsgs(lineNbr, fileName, text, arg, NULL));
}

void	_putErrmsg(const char *fileName, int lineNbr, const char *text,
		const char *arg)
{
	_postErrmsg(fileName, lineNbr, text, arg);
	writeErrmsgMemos();
}

void	_postSysErrmsg(const char *fileName, int lineNbr, const char *text,
		const char *arg)
{
	char	*sysmsg;
	int	textLength;
	int	maxTextLength;
	char	textBuffer[1024];

	if (text)
	{
		textLength = strlen(text);
		sysmsg = system_error_msg();
		maxTextLength = sizeof textBuffer - (2 + strlen(sysmsg) + 1);
		if (textLength > maxTextLength)
		{
			textLength = maxTextLength;
		}

		isprintf(textBuffer, sizeof textBuffer, "%.*s: %s",
				textLength, text, sysmsg);
		_postErrmsg(fileName, lineNbr, textBuffer, arg);
	}
}

void	_putSysErrmsg(const char *fileName, int lineNbr, const char *text,
		const char *arg)
{
	_postSysErrmsg(fileName, lineNbr, text, arg);
	writeErrmsgMemos();
}

int	getErrmsg(char *buffer)
{
	if (buffer == NULL)
	{
		ABORT_AS_REQD;
		return 0;
	}

	return _errmsgs(0, NULL, NULL, NULL, buffer);
}

void	writeErrmsgMemos()
{
	static ResourceLock	memosLock;
	static int		memosLockInit = 0;
	static char		msgwritebuf[ERRMSGS_BUFSIZE];
	static char		*omissionMsg = "[?] message omitted due to \
excessive length";

	/*	Because buffer is static, it is shared.  So access
	 *	to it must be mutexed.					*/

	if (!memosLockInit)
	{
		memset((char *) &memosLock, 0, sizeof(ResourceLock));
		if (initResourceLock(&memosLock) < 0)
		{
			ABORT_AS_REQD;
			return;
		}

		memosLockInit = 1;
	}

	lockResource(&memosLock);
	while (1)
	{
		if (getErrmsg(msgwritebuf) == 0)
		{
			break;
		}

		if (msgwritebuf[0] == '\n')
		{
			writeMemo(omissionMsg);
		}
		else
		{
			writeMemo(msgwritebuf);
		}
	}

	unlockResource(&memosLock);
}

int	_coreFileNeeded(int *ctrl)
{
	static int	coreFileNeeded = CORE_FILE_NEEDED;

	if (ctrl)
	{
		coreFileNeeded = *ctrl;
	}

	return coreFileNeeded;
}

int	_iEnd(const char *fileName, int lineNbr, const char *arg)
{
	_postErrmsg(fileName, lineNbr, "Assertion failed.", arg);
	writeErrmsgMemos();
	printStackTrace();
    ABORT_AS_REQD;

	return 1;
}

void	printStackTrace()
{
#if (defined(bionic) || defined(uClibc) || !(defined(linux)))
	writeMemo("[?] No stack trace available on this platform.");
#else
#define	MAX_TRACE_DEPTH	100
	void	*returnAddresses[MAX_TRACE_DEPTH];
	size_t	stackFrameCount;
	char	**functionNames;
	int	i;

	stackFrameCount = backtrace(returnAddresses, MAX_TRACE_DEPTH);
	functionNames = backtrace_symbols(returnAddresses, stackFrameCount);
	if (functionNames == NULL)
	{
		writeMemo("[!] Can't print backtrace function names.");
		return;
	}

	writeMemo("[i] Current stack trace:");
	for (i = 0; i < stackFrameCount; i++)
	{
		writeMemoNote("[i] ", functionNames[i]);
	}

	free(functionNames);
#endif
}

void	encodeSdnv(Sdnv *sdnv, uvast val)
{
	static uvast	sdnvMask = ((uvast) -1) / 128;
	uvast		remnant;
	int		i;
	unsigned char	flag = 0;
	unsigned char	*text;

	/*	Get length of SDNV text: one byte for each 7 bits of
	 *	significant numeric value.  On each iteration of the
	 *	loop, until what's left of the original value is zero,
	 *	shift the remaining value 7 bits to the right and add
	 *	1 to the imputed SDNV length.				*/

	CHKVOID(sdnv);
	sdnv->length = 0;
	remnant = val;
	do
	{
		remnant = (remnant >> 7) & sdnvMask;
		(sdnv->length)++;
	} while (remnant > 0);

	/*	Now fill the SDNV text from the numeric value bits.	*/

	text = sdnv->text + sdnv->length;
	i = sdnv->length;
	remnant = val;
	while (i > 0)
	{
		text--;

		/*	Get low-order 7 bits of what's left, OR'ing
		 *	it with high-order bit flag for this position
		 *	of the SDNV.					*/

		*text = (remnant & 0x7f) | flag;

		/*	Shift those bits out of the value.		*/

		remnant = (remnant >> 7) & sdnvMask;
		flag = 0x80;		/*	Flag is now 1.		*/
		i--;
	}
}

int	decodeSdnv(uvast *val, unsigned char *sdnvTxt)
{
	int		sdnvLength = 0;
	unsigned char	*cursor;

	CHKZERO(val);
	CHKZERO(sdnvTxt);
	*val = 0;
	cursor = sdnvTxt;
	while (1)
	{
		sdnvLength++;
		if (sdnvLength > 10)
		{
			return 0;	/*	More than 70 bits.	*/
		}

		/*	Shift numeric value 7 bits to the left (that
		 *	is, multiply by 128) to make room for 7 bits
		 *	of SDNV byte value.				*/

		*val <<= 7;

		/*	Insert SDNV byte value (with its high-order
		 *	bit masked off) as low-order 7 bits of the
		 *	numeric value.					*/

		*val |= (*cursor & 0x7f);
		if ((*cursor & 0x80) == 0)	/*	Last SDNV byte.	*/
		{
			return sdnvLength;
		}

		/*	Haven't reached the end of the SDNV yet.	*/

		cursor++;
	}
}

void	loadScalar(Scalar *s, signed int i)
{
	CHKVOID(s);
	if (i < 0)
	{
		i = 0 - i;
	}

	s->gigs = 0;
	s->units = i;
	while (s->units >= ONE_GIG)
	{
		s->gigs++;
		s->units -= ONE_GIG;
	}
}

void	increaseScalar(Scalar *s, signed int i)
{
	CHKVOID(s);
	if (i < 0)
	{
		i = 0 - i;
	}

	while (i >= ONE_GIG)
	{
		i -= ONE_GIG;
		s->gigs++;
	}

	s->units += i;
	while (s->units >= ONE_GIG)
	{
		s->gigs++;
		s->units -= ONE_GIG;
	}
}

void	reduceScalar(Scalar *s, signed int i)
{
	CHKVOID(s);
	if (i < 0)
	{
		i = 0 - i;
	}

	while (i >= ONE_GIG)
	{
		i -= ONE_GIG;
		s->gigs--;
	}

	while (i > s->units)
	{
		s->units += ONE_GIG;
		s->gigs--;
	}

	s->units -= i;
}

void	multiplyScalar(Scalar *s, signed int i)
{
	double	product;

	CHKVOID(s);
	if (i < 0)
	{
		i = 0 - i;
	}

	product = ((((double)(s->gigs)) * ONE_GIG) + (s->units)) * i;
	s->gigs = (int) (product / ONE_GIG);
	s->units = (int) (product - (((double)(s->gigs)) * ONE_GIG));
}

void	divideScalar(Scalar *s, signed int i)
{
	double	quotient;

	CHKVOID(s);
	CHKVOID(i != 0);
	if (i < 0)
	{
		i = 0 - i;
	}

	quotient = ((((double)(s->gigs)) * ONE_GIG) + (s->units)) / i;
	s->gigs = (int) (quotient / ONE_GIG);
	s->units = (int) (quotient - (((double)(s->gigs)) * ONE_GIG));
}

void	copyScalar(Scalar *to, Scalar *from)
{
	CHKVOID(to);
	CHKVOID(from);
	to->gigs = from->gigs;
	to->units = from->units;
}

void	addToScalar(Scalar *s, Scalar *increment)
{
	CHKVOID(s);
	CHKVOID(increment);
	increaseScalar(s, increment->units);
	s->gigs += increment->gigs;
}

void	subtractFromScalar(Scalar *s, Scalar *decrement)
{
	CHKVOID(s);
	CHKVOID(decrement);
	reduceScalar(s, decrement->units);
	s->gigs -= decrement->gigs;
}

int	scalarIsValid(Scalar *s)
{
	CHKZERO(s);
	return (s->gigs >= 0);
}

void	scalarToSdnv(Sdnv *sdnv, Scalar *scalar)
{
	int		gigs;
	int		units;
	int		i;
	unsigned char	flag = 0;
	unsigned char	*cursor;

	CHKVOID(scalarIsValid(scalar));
	CHKVOID(sdnv);
	sdnv->length = 0;

	/*		Calculate sdnv length				*/

	gigs = scalar->gigs;
	units = scalar->units;
	if (gigs) 
	{
		/*	The scalar is greater than 2^30 - 1, so start
		 *	with the length occupied by all 30 bits of
		 *	"units" in the scalar.  This will occupy 5
		 *	bytes in the sdnv with room for an additional
		 *	5 high-order bits.  These bits will be the
		 *	low-order 5 bits of gigs.  If the value in
		 *	gigs is greater than 2^5 -1, increase sdnv
		 *	length accordingly.				*/

		sdnv->length += 5;			
		gigs >>= 5;
		while (gigs)
		{
			gigs >>= 7;
			sdnv->length++;
		}
	}
	else
	{
		/*	gigs = 0, so calculate the sdnv length from
			units only.					*/

		do
		{
			units >>= 7;
			sdnv->length++;
		} while (units);
	}

	/*		Fill the sdnv text.				*/

	cursor = sdnv->text + sdnv->length;
	i = sdnv->length;
	gigs = scalar->gigs;
	units = scalar->units;
	do
	{
		cursor--;

		/*	Start filling the sdnv text from the last byte.
			Get 7 low-order bits from units and add the
			flag to the high-order bit. Flag is 0 for the
			last byte and 1 for all the previous bytes.	*/

		*cursor = (units & 0x7f) | flag;
		units >>= 7;
		flag = 0x80;		/*	Flag is now 1.		*/
		i--;
	} while (units);

	if (gigs)
	{
		while (sdnv->length - i < 5)
		{
			cursor--;

			/* Fill remaining sdnv bytes corresponding to
			   units with zeroes.				*/

			*cursor = 0x00 | flag;
			i--;
		}

		/*	Place the 5 low-order bits of gigs in the
			current	sdnv byte.				*/

		*cursor |= ((gigs & 0x1f) << 2);
		gigs >>= 5;
		while (i)
		{
			cursor--;

			/*	Now fill the remaining sdnv bytes
				from gigs.				*/

			*cursor = (gigs & 0x7f) | flag;
			gigs >>= 7;
			i--;
		}
	}
}

int	sdnvToScalar(Scalar *scalar, unsigned char *sdnvText)
{
	int		sdnvLength;
	int		i;
	int		numSize = 0; /* Size of stored number in bits.	*/
	unsigned char	*cursor;
	unsigned char	flag;
	unsigned char	k;

	CHKZERO(scalar);
	CHKZERO(sdnvText);
	cursor = sdnvText;

	/*	Find out the sdnv length and size of stored number,
	 *	stripping off all leading zeroes.			*/

	flag = (*cursor & 0x80);/*	Get flag of 1st byte.		*/
	k = *cursor << 1;	/*	Discard the flag bit.		*/
	i = 7;
	while (i)
	{
		if (k & 0x80)
		{
			break;	/*	Loop until a '1' is found.	*/
		}

		i--;
		k <<= 1;
	}

	numSize += i;	/*	Add significant bits from first byte.	*/
	if (flag)	/*	Not end of SDNV.			*/
	{
		/*	Sdnv has more than one byte.  Add 7 bits for
		 *	the last byte and advance cursor to add the
		 *	bits for all intermediate bytes.		*/

		numSize += 7;
		cursor++;
		while (*cursor & 0x80)
		{
			numSize += 7;
			cursor++;
		}
	}

	if (numSize > 61)
	{
		return 0;	/*	Too long to fit in a Scalar.	*/
	}

	sdnvLength = (cursor - sdnvText) + 1;

	/*		Now start filling gigs and units.		*/

	scalar->gigs = 0;
	scalar->units = 0;
	cursor = sdnvText;
	i = sdnvLength;

	while (i > 5)
	{	/*	Sdnv bytes containing gigs only.		*/

		scalar->gigs <<= 7;
		scalar->gigs |= (*cursor & 0x7f);
		cursor++;
		i--;
	}

	if (i == 5)
	{	/* Sdnv byte containing units and possibly gigs too.	*/

		if (numSize > 30)
		{
			/* Fill the gigs bits after shifting out
			   the 2 bits that belong to units.		*/

			scalar->gigs <<= 5;
			scalar->gigs |= ((*cursor >> 2) & 0x1f);
		}

		/*		Fill the units bits.			*/

		scalar->units = (*cursor & 0x03);
		cursor++;
		i--;
	}

	while (i)
	{	/*	Sdnv bytes containing units only.		*/

		scalar->units <<= 7;
		scalar->units |= (*cursor & 0x7f);
		cursor++;
		i--;
	}

	return sdnvLength;
}

uvast	htonv(uvast hostvast)
{
	static const int	fortyTwo = 42;

	if ((*(char *) &fortyTwo) == 0)	/*	Check first byte.	*/
	{
		/*	Small-endian (network byte order) machine.	*/

		return hostvast;
	}

	/*	Must  reverse the byte order of this number.		*/

#if (!LONG_LONG_OKAY)
	return htonl(hostvast);
#else
	static const vast	mask = 0xffffffff;
	unsigned int		big_part;
	unsigned int		small_part;
	uvast			result;

	big_part = hostvast >> 32;
	small_part = hostvast & mask;
	big_part = htonl(big_part);
	small_part = htonl(small_part);
	result = small_part;
	return (result << 32) | big_part;
#endif
}

uvast	ntohv(uvast netvast)
{
	return htonv(netvast);
}

int	fullyQualified(char *fileName)
{
	CHKZERO(fileName);

#if (defined(VXWORKS))
	if (strncmp("host:", fileName, 5) == 0)
	{
		fileName += 5;
	}

	if (isalpha((int)*fileName) && *(fileName + 1) == ':')
	{
		return 1;
	}

	if (*fileName == '/')
	{
		return 1;
	}

	return 0;

#elif (defined(mingw) || defined(DOS_PATH_DELIMITER))
	if (isalpha(*fileName) && *(fileName + 1) == ':')
	{
		return 1;
	}

	return 0;
#else
	if (*fileName == '/')
	{
		return 1;
	}

	return 0;
#endif
}

int	qualifyFileName(char *fileName, char *buffer, int buflen)
{
	char	pathDelimiter = ION_PATH_DELIMITER;
	int	nameLen;
	int	cwdLen;

	CHKERR(fileName);
	CHKERR(buffer);
	CHKERR(buflen> 0);
	nameLen = strlen(fileName);
	if (fullyQualified(fileName))
	{
		if (nameLen < buflen)
		{
			istrcpy(buffer, fileName, buflen);
			return 0;
		}

		writeMemoNote("[?] File name is too long for qual. buffer.",
				fileName);
		return -1;
	}

	/*	This is a relative path name; must insert cwd.		*/

	if (igetcwd(buffer, buflen) == NULL)
	{
		putErrmsg("Can't get cwd.", NULL);
		return -1;
	}

	cwdLen = strlen(buffer);
	if ((cwdLen + 1 + nameLen + 1) > buflen)
	{
		writeMemoNote("Qualified file name would be too long.",
				fileName);
		return -1;
	}

	*(buffer + cwdLen) = pathDelimiter;
	cwdLen++;		/*	cwdname including delimiter	*/
	istrcpy(buffer + cwdLen, fileName, buflen - cwdLen);
	return 0;
}

void	findToken(char **cursorPtr, char **token)
{
	char	*cursor;

	if (token == NULL)
	{
		ABORT_AS_REQD;
		return;
	}

	*token = NULL;		/*	The default.			*/
	if (cursorPtr == NULL || (*cursorPtr) == NULL)
	{
		ABORT_AS_REQD;
		return;
	}

	cursor = *cursorPtr;

	/*	Skip over any leading whitespace.			*/

	while (isspace((int) *cursor))
	{
		cursor++;
	}

	if (*cursor == '\0')	/*	Nothing but whitespace.		*/
	{
		*cursorPtr = cursor;
		return;
	}

	/*	Token delimited by quotes is the complicated case.	*/

	if (*cursor == '\'')	/*	Quote-delimited token.		*/
	{
		/*	Token is everything after this single quote,
		 *	up to (but not including) the next non-escaped
		 *	single quote.					*/

		cursor++;
		while (*cursor != '\0')
		{
			if (*token == NULL)
			{
				*token = cursor;
			}

			if (*cursor == '\\')	/*	Escape.		*/
			{
				/*	Include the escape character
				 *	plus the following (escaped)
				 *	character (unless it's the end
				 *	of the string) in the token.	*/

				cursor++;
				if (*cursor == '\0')
				{
					*cursorPtr = cursor;
					return;	/*	unmatched quote	*/
				}

				cursor++;
				continue;
			}

			if (*cursor == '\'')	/*	End of token.	*/
			{
				*cursor = '\0';
				cursor++;
				*cursorPtr = cursor;
				return;		/*	matched quote	*/
			}

			cursor++;
		}

		/*	If we get here it's another case of unmatched
		 *	quote, but okay.				*/

		*cursorPtr = cursor;
		return;
	}

	/*	The normal case: a simple whitespace-delimited token.
	 *	Token is this character and all successive characters
	 *	up to (but not including) the next whitespace.		*/

	*token = cursor;
	cursor++;
	while (*cursor != '\0')
	{
		if (isspace((int) *cursor))	/*	End of token.	*/
		{
			*cursor = '\0';
			cursor++;
			break;
		}

		cursor++;
	}

	*cursorPtr = cursor;
}

#ifdef ION_NO_DNS
unsigned int	getAddressOfHost()
{
	return 0;
}

char	*addressToString(struct in_addr address, char *buffer)
{
	CHKNULL(buffer);

	*buffer = 0;
	putErrmsg("Can't convert IP address to string.", NULL);
	return buffer;
}

#else

unsigned int	getAddressOfHost()
{
	char	hostnameBuf[MAXHOSTNAMELEN + 1];

	getNameOfHost(hostnameBuf, sizeof hostnameBuf);
	return getInternetAddress(hostnameBuf);
}

char	*addressToString(struct in_addr address, char *buffer)
{
	char	*result;

	CHKNULL(buffer);
	*buffer = 0;
#if defined (VXWORKS)
	inet_ntoa_b(address, buffer);
#else
	result = inet_ntoa(address);
	if (result == NULL)
	{
		putSysErrmsg("inet_ntoa() returned NULL", NULL);
	}
	else
	{
		istrcpy(buffer, result, 16);
	}
#endif
	return buffer;
}
#endif	/*	ION_NO_DNS						*/

#if (defined(FSWLAN) || !(defined(ION_NO_DNS)))
int	parseSocketSpec(char *socketSpec, unsigned short *portNbr,
		unsigned int *ipAddress)
{
	char		*delimiter;
	char		*hostname;
	char		hostnameBuf[MAXHOSTNAMELEN + 1];
	unsigned int	i4;

	CHKERR(portNbr);
	CHKERR(ipAddress);
	*portNbr = 0;			/*	Use default port nbr.	*/
	*ipAddress = INADDR_ANY;	/*	Use local host address.	*/

	if (socketSpec == NULL || *socketSpec == '\0')
	{
		return 0;		/*	Use defaults.		*/
	}

	delimiter = strchr(socketSpec, ':');
	if (delimiter)
	{
		*delimiter = '\0';	/*	Delimit host name.	*/
	}

	/*	First figure out the IP address.  @ is local host.	*/

	hostname = socketSpec;
	if (strlen(hostname) != 0)
	{
		if (strcmp(hostname, "0.0.0.0") == 0)
		{
			*ipAddress = INADDR_ANY;
		}
		else
		{
			if (strcmp(hostname, "@") == 0)
			{
				getNameOfHost(hostnameBuf, sizeof hostnameBuf);
				hostname = hostnameBuf;
			}

			i4 = getInternetAddress(hostname);
			if (i4 < 1)	/*	Invalid hostname.	*/
			{
				writeMemoNote("[?] Can't get IP address",
						hostname);
				if (delimiter)
				{
					/*	Back out the parsing
					 *	of the socket spec.	*/

					*delimiter = ':';
				}

				return -1;
			}
			else
			{
				*ipAddress = i4;
			}
		}
	}

	/*	Now pick out the port number, if requested.		*/

	if (delimiter == NULL)		/*	No port number.		*/
	{
		return 0;		/*	All done.		*/
	}

	*delimiter = ':';		/*	Back out the parsing.	*/
	i4 = atoi(delimiter + 1);	/*	Get port number.	*/
	if (i4 != 0)
	{
		if (i4 < 1024 || i4 > 65535)
		{
			writeMemoNote("[?] Invalid port number.", utoa(i4));
			return -1;
		}
		else
		{
			*portNbr = i4;
		}
	}

	return 0;
}
#else
int	parseSocketSpec(char *socketSpec, unsigned short *portNbr,
		unsigned int *ipAddress)
{
	return 0;
}
#endif	/*	defined(FSWLAN || !(defined(ION_NO_DNS)))		*/

void	printDottedString(unsigned int hostNbr, char *buffer)
{
	CHKVOID(buffer);
	isprintf(buffer, 16, "%u.%u.%u.%u", (hostNbr >> 24) & 0xff,
		(hostNbr >> 16) & 0xff, (hostNbr >> 8) & 0xff, hostNbr & 0xff);
}

/*	Portable implementation of a safe snprintf: always NULL-
 *	terminates the content of the string composition buffer.	*/

#define SN_FMT_SIZE		64

/*	Flag array indices	*/
#define	SN_LEFT_JUST		0
#define	SN_SIGNED		1
#define	SN_SPACE_PREFIX		2
#define	SN_PAD_ZERO		3
#define	SN_ALT_OUTPUT		4

static void	snGetFlags(char **cursor, char *fmt, int *fmtLen)
{
	int	flags[5];

	/*	Copy all flags to field print format.  No flag is
	 *	copied more than once.					*/

	memset((char *) flags, 0, sizeof flags);
	while (1)
	{	
		switch (**cursor)
		{
		case '-':
			if (flags[SN_LEFT_JUST] == 0)
			{
				*(fmt + *fmtLen) = **cursor;
				(*fmtLen)++;
				flags[SN_LEFT_JUST] = 1;
			}

			break;

		case '+':
			if (flags[SN_SIGNED] == 0)
			{
				*(fmt + *fmtLen) = **cursor;
				(*fmtLen)++;
				flags[SN_SIGNED] = 1;
			}

			break;

		case ' ':
			if (flags[SN_SPACE_PREFIX] == 0)
			{
				*(fmt + *fmtLen) = **cursor;
				(*fmtLen)++;
				flags[SN_SPACE_PREFIX] = 1;
			}

			break;

		case '0':
			if (flags[SN_PAD_ZERO] == 0)
			{
				*(fmt + *fmtLen) = **cursor;
				(*fmtLen)++;
				flags[SN_PAD_ZERO] = 1;
			}

			break;

		case '#':
			if (flags[SN_ALT_OUTPUT] == 0)
			{
				*(fmt + *fmtLen) = **cursor;
				(*fmtLen)++;
				flags[SN_ALT_OUTPUT] = 1;
			}

			break;

		default:
			return;	/*	No more flags for field.	*/
		}

		(*cursor)++;
	}
}

static void	snGetNumber(char **cursor, char *fmt, int *fmtLen, int *number)
{
	int	numDigits = 0;
	char	digit;

	while (1)
	{
		digit = **cursor;
		if (digit < '0' || digit > '9')
		{
			return;	/*	No more digits in number.	*/
		}

		/*	Accumulate number value.			*/

		digit -= 48;	/*	Convert from ASCII.		*/
		if ((*number) < 0)	/*	First digit.		*/
		{
			(*number) = digit;
		}
		else
		{
			(*number) = (*number) * 10;
			(*number) += digit;
		}

		/*	Copy to field format if possible.  Largest
		 *	possible value in a 32-bit number is about
		 *	4 billion, represented in 10 decimal digits.
		 *	Largest possible value in a 64-bit number is
		 *	the square of that value, represented in no
		 *	more than 21 decimal digits.  So any number
		 *	of more than 21 decimal digits is invalid.	*/

		numDigits++;
		if (numDigits < 22)
		{
			*(fmt + *fmtLen) = **cursor;
			(*fmtLen)++;
		}

		(*cursor)++;
	}
}

int	_isprintf(char *buffer, int bufSize, char *format, ...)
{
	va_list		args;
	char		*cursor;
	int		stringLength = 0;
	int		printLength = 0;
	char		fmt[SN_FMT_SIZE];
	int		fmtLen;
	int		minFieldLength;
	int		precision;
	char		scratchpad[64];
	int		numLen;
	int		fieldLength;
	int		isLongLong;		/*	Boolean		*/
	int		*ipval;
	char		*sval;
	int		ival;
	long long	llval;
	double		dval;
	void		*vpval;
	uaddr		uaddrval;

	if (buffer == NULL || bufSize < 1)
	{
		ABORT_AS_REQD;
		return 0;
	}

	if (format == NULL)
	{
		ABORT_AS_REQD;
		if (bufSize < 2)
		{
			*buffer = '\0';
		}
		else
		{
			*buffer = '?';
			*(buffer + 1) = '\0';
		}

		return 0;
	}

	va_start(args, format);
	for (cursor = format; *cursor != '\0'; cursor++)
	{
		if (*cursor != '%')
		{
			if ((stringLength + 1) < bufSize)
			{
				*(buffer + stringLength) = *cursor;
				printLength++;
			}

			stringLength++;
			continue;
		}

		/*	We've encountered a variable-length field in
		 *	the string.					*/

		minFieldLength = -1;	/*	Indicates none.		*/
		precision = -1;		/*	Indicates none.		*/

		/*	Start extracting the field format so that
		 *	we can use sprintf to figure out the length
		 *	of the field.					*/

		fmt[0] = '%';
		fmtLen = 1;
		cursor++;

		/*	Copy any flags for field.			*/

		snGetFlags(&cursor, fmt, &fmtLen);

		/*	Copy the minimum length of field, if present.	*/

		if (*cursor == '*')
		{
			cursor++;
			minFieldLength = va_arg(args, int);
			if (minFieldLength < 0)
			{
				minFieldLength = -1;	/*	None.	*/
			}
			else
			{
				sprintf(scratchpad, "%d", minFieldLength);
				numLen = strlen(scratchpad);
				memcpy(fmt + fmtLen, scratchpad, numLen);
				fmtLen += numLen;
			}
		}
		else
		{
			snGetNumber(&cursor, fmt, &fmtLen, &minFieldLength);
		}

		if (*cursor == '.')	/*	Start of precision.	*/
		{
			fmt[fmtLen] = '.';
			fmtLen++;
			cursor++;

			/*	Copy the precision of the field.	*/

			if (*cursor == '*')
			{
				cursor++;
				precision = va_arg(args, int);
				if (precision < 0)
				{
					precision = -1;	/*	None.	*/
				}
				else
				{
					sprintf(scratchpad, "%d", precision);
					numLen = strlen(scratchpad);
					memcpy(fmt + fmtLen, scratchpad,
							numLen);
					fmtLen += numLen;
				}
			}
			else
			{
				snGetNumber(&cursor, fmt, &fmtLen, &precision);
			}
		}

		/*	Copy the field's length modifier, if any.	*/

		isLongLong = 0;
		if ((*cursor) == 'h'		/*	Short.		*/
		|| (*cursor) == 'L')		/*	Long double.	*/
		{
			fmt[fmtLen] = *cursor;
			fmtLen++;
			cursor++;
		}
		else
		{
			if ((*cursor) == 'l')	/*	Long...		*/
			{
				fmt[fmtLen] = *cursor;
				fmtLen++;
				cursor++;
				if ((*cursor) == 'l')	/*	Vast.	*/
				{
					isLongLong = 1;
					fmt[fmtLen] = *cursor;
					fmtLen++;
					cursor++;
				}
			}
			else
			{
				if ((*cursor) == 'I'
				&& (*(cursor + 1)) == '6'
				&& (*(cursor + 2)) == '4')
				{
#ifdef mingw
					isLongLong = 1;
					fmt[fmtLen] = *cursor;
					fmtLen++;
					cursor++;
					fmt[fmtLen] = *cursor;
					fmtLen++;
					cursor++;
					fmt[fmtLen] = *cursor;
					fmtLen++;
					cursor++;
#endif
				}
			}
		}

		/*	Handle a couple of weird conversion characters
		 *	as applicable.					*/

		if (*cursor == 'n')	/*	Report on string size.	*/
		{
			ipval = va_arg(args, int *);
			if (ipval)
			{
				*ipval = stringLength;
			}

			continue;
		}

		if (*cursor == '%')	/*	Literal '%' in string.	*/
		{
			if ((stringLength + 1) < bufSize)
			{
				*(buffer + stringLength) = '%';
				printLength++;
			}

			stringLength++;
			continue;	/*	No argument consumed.	*/
		}

		/*	Ready to compute field length.			*/

		fmt[fmtLen] = *cursor;	/*	Copy conversion char.	*/
		fmtLen++;
		fmt[fmtLen] = '\0';	/*	Terminate format.	*/

		/*	Handle string field conversion character.	*/

		if (*cursor == 's')
		{
			sval = va_arg(args, char *);
			if (sval == NULL)
			{
				continue;
			}

			fieldLength = strlen(sval);

			/*	Truncate per precision.			*/

			if (precision != -1 && precision < fieldLength)
			{
				fieldLength = precision;
			}

			/*	Add padding as per minFieldLength.	*/

			if (minFieldLength != -1
			&& fieldLength < minFieldLength)
			{
				fieldLength = minFieldLength;
			}

			if (stringLength + fieldLength < bufSize)
			{
				sprintf(buffer + stringLength, fmt, sval);
				printLength += fieldLength;
			}

			stringLength += fieldLength;
			continue;
		}

		/*	Handle numeric field conversion character.	*/

		switch (*cursor)
		{
		case 'd':
		case 'u':
		case 'i':
		case 'o':
		case 'x':
		case 'X':
		case 'c':
			if (isLongLong)
			{
				llval = va_arg(args, long long);
				sprintf(scratchpad, fmt, llval);
			}
			else
			{
				ival = va_arg(args, int);
				sprintf(scratchpad, fmt, ival);
			}

			break;

		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
			dval = va_arg(args, double);
			sprintf(scratchpad, fmt, dval);
			break;

		case 'p':
			vpval = va_arg(args, void *);
			uaddrval = (uaddr) vpval;
			sprintf(scratchpad, ADDR_FIELDSPEC, uaddrval);
			break;

		default:		/*	Bad conversion char.	*/
			continue;	/*	No argument consumed.	*/
		}

		fieldLength = strlen(scratchpad);
		if (stringLength + fieldLength < bufSize)
		{
			memcpy(buffer + stringLength, scratchpad, fieldLength);
			printLength += fieldLength;
		}

		stringLength += fieldLength;
	}

	va_end(args);

	/*	NULL-terminate the buffer contents, one way or another.	*/

	if (stringLength < bufSize)
	{
		*(buffer + stringLength) = '\0';
	}
	else
	{
		*(buffer + printLength) = '\0';
	}

	return stringLength;
}

/*	*	*	Other portability adaptations	*	*	*/

size_t	istrlen(const char *from, size_t maxlen)
{
	size_t	length;
	const char	*cursor;

	if (from == NULL)
	{
		ABORT_AS_REQD;
		return 0;
	}

	length = 0;
	if (maxlen > 0)
	{
		for (cursor = from; *cursor; cursor++)
		{
			length++;
			if (length == maxlen)
			{
				break;
			}
		}
	}

	return length;
}

char	*istrcpy(char *buffer, const char *from, size_t bufSize)
{
	int	maxText;
	int	copySize;

	if (buffer == NULL || from == NULL || bufSize < 1)
	{
		ABORT_AS_REQD;
		return NULL;
	}

	maxText = bufSize - 1;
	copySize = istrlen(from, maxText);
	memcpy(buffer, from, copySize);
	*(buffer + copySize) = '\0';
	return buffer;
}

char	*istrcat(char *buffer, char *from, size_t bufSize)
{
	int	maxText;
	int	currTextSize;
	int	maxCopy;
	int	copySize;

	if (buffer == NULL || from == NULL || bufSize < 1)
	{
		ABORT_AS_REQD;
		return NULL;
	}

	maxText = bufSize - 1;
	currTextSize = istrlen(buffer, maxText);
	maxCopy = maxText - currTextSize;
	copySize = istrlen(from, maxCopy);
	memcpy(buffer + currTextSize, from, copySize);
	*(buffer + currTextSize + copySize) = '\0';
	return buffer;
}

char	*igetcwd(char *buf, size_t size)
{
	char	*cwdName;

	CHKNULL(buf);
	CHKNULL(size > 0);
	cwdName = getcwd(buf, size);
	if (cwdName == NULL)
	{
		putSysErrmsg("Can't get CWD name", itoa(size));
	}

	return cwdName;
}

#ifdef POSIX_TASKS

#ifndef SIGNAL_RULE_CT
#define SIGNAL_RULE_CT	100
#endif

typedef struct
{
	int		declared;	/*	Boolean.		*/
	pthread_t	tid;
	int		signbr;
	SignalHandler	handler;
} SignalRule;

static SignalHandler	_signalRules(int signbr, SignalHandler handler)
{
	static SignalRule	rules[SIGNAL_RULE_CT];
	static int		rulesInitialized = 0;
	int			i;
	pthread_t		tid = sm_TaskIdSelf();
	SignalRule		*rule;

	if (!rulesInitialized)
	{
		memset((char *) rules, 0, sizeof rules);
		rulesInitialized = 1;
	}

	if (handler)	/*	Declaring a new signal rule.		*/
	{
		/*	We take this as an opportunity to clear out any
 		 *	existing rules that are no longer needed, due to
 		 *	termination of the threads that declared them.	*/

		for (i = 0, rule = rules; i < SIGNAL_RULE_CT; i++, rule++)
		{
			if (rule->declared == 0)	/*	Clear.	*/
			{
				if (handler == NULL)	/*	Noted.	*/
				{
					continue;
				}

				/*	Declare new signal rule here.	*/

				rule->declared = 1;
				rule->tid = tid;
				rule->signbr = signbr;
				rule->handler = handler;
				handler = NULL;		/*	Noted.	*/
				continue;
			}

			/*	This is a declared signal rule.		*/

			if (pthread_equal(rule->tid, tid))
			{
				/*	One of thread's own rules.	*/

				if (rule->signbr != signbr)
				{
					continue;	/*	Okay.	*/
				}

				/*	New handler for tid/signbr.	*/

				if (handler)	/*	Not noted yet.	*/
				{
					rule->handler = handler;
					handler = NULL;	/*	Noted.	*/
				}
				else	/*	Noted in another rule.	*/
				{
					rule->declared = 0;
				}

				continue;
			}

			/*	Signal rule for another thread.		*/

			if (!sm_TaskExists(rule->tid))
			{
				/*	Obsolete rule; thread is gone.	*/

				rule->declared = 0;	/*	Clear.	*/
			}
		}

		return NULL;
	}

	/*	Just looking up applicable signal rule for tid/signbr.	*/

	for (i = 0, rule = rules; i < SIGNAL_RULE_CT; i++, rule++)
	{
		if (pthread_equal(rule->tid, tid) && rule->signbr == signbr)
		{
			return rule->handler;
		}
	}

	return NULL;	/*	No applicable signal rule.		*/
}

static void	threadSignalHandler(int signbr)
{
	SignalHandler	handler = _signalRules(signbr, NULL);

	if (handler)
	{
		handler(signbr);
	}
}
#endif	/*	end of #ifdef POSIX_TASKS				*/

void	isignal(int signbr, void (*handler)(int))
{
	struct sigaction	action;
#ifdef POSIX_TASKS
	sigset_t		signals;

	oK(sigemptyset(&signals));
	oK(sigaddset(&signals, signbr));
	oK(pthread_sigmask(SIG_UNBLOCK, &signals, NULL));
	oK(_signalRules(signbr, handler));
	handler = threadSignalHandler;
#endif	/*	end of #ifdef POSIX_TASKS				*/
	memset((char *) &action, 0, sizeof(struct sigaction));
	action.sa_handler = handler;
	oK(sigaction(signbr, &action, NULL));
#ifdef freebsd
	oK(siginterrupt(signbr, 1));
#endif
}

void	iblock(int signbr)
{
	sigset_t	signals;

	oK(sigemptyset(&signals));
	oK(sigaddset(&signals, signbr));
	oK(pthread_sigmask(SIG_BLOCK, &signals, NULL));
}

char	*igets(int fd, char *buffer, int buflen, int *lineLen)
{
	char	*cursor = buffer;
	int	maxLine = buflen - 1;
	int	len;

	if (fd < 0 || buffer == NULL || buflen < 1 || lineLen == NULL)
	{
		ABORT_AS_REQD;
		putErrmsg("Invalid argument(s) passed to igets().", NULL);
		return NULL;
	}

	len = 0;
	while (1)
	{
		switch (read(fd, cursor, 1))
		{
		case 0:		/*	End of file; also end of line.	*/
			if (len == 0)		/*	Nothing more.	*/
			{
				*(buffer + len) = '\0';
				*lineLen = len;
				return NULL;	/*	Indicate EOF.	*/
			}

			/*	End of last line.			*/

			break;			/*	Out of switch.	*/

		case -1:
			if (errno == EINTR)	/*	Treat as EOF.	*/
			{
				*(buffer + len) = '\0';
				*lineLen = 0;
				return NULL;
			}

			putSysErrmsg("Failed reading line", itoa(len));
			*(buffer + len) = '\0';
			*lineLen = -1;
			return NULL;

		default:
			if (*cursor == 0x0a)		/*	LF (nl)	*/
			{
				/*	Have reached end of line.	*/

				if (len > 0
				&& *(buffer + (len - 1)) == 0x0d)
				{
					len--;		/*	Lose CR	*/
				}

				break;		/*	Out of switch.	*/
			}

			/*	Have not reached end of line yet.	*/

			if (len == maxLine)	/*	Must truncate.	*/
			{
				break;		/*	Out of switch.	*/
			}

			/*	Okay, include this char in the line...	*/

			len++;

			/*	...and read the next character.		*/

			cursor++;
			continue;
		}

		break;				/*	Out of loop.	*/
	}

	*(buffer + len) = '\0';
	*lineLen = len;
	return buffer;
}

int	iputs(int fd, char *string)
{
	int	totalBytesWritten = 0;
	int	length;
	int	bytesWritten;

	if (fd < 0 || string == NULL)
	{
		ABORT_AS_REQD;
		putErrmsg("Invalid argument(s) passed to iputs().", NULL);
		return -1;
	}

	length = strlen(string);
	while (totalBytesWritten < length)
	{
		bytesWritten = write(fd, string + totalBytesWritten,
				length - totalBytesWritten);
		if (bytesWritten < 0)
		{
			putSysErrmsg("Failed writing line",
					itoa(totalBytesWritten));
			return -1;
		}

		totalBytesWritten += bytesWritten;
	}

	return totalBytesWritten;
}
