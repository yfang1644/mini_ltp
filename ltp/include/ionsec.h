/*
	ionsec.h:	definition of the application programming
			interface for accessing the information inx
		       	ION's security database.

	Copyright (c) 2009, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.

	Author: Scott Burleigh, Jet Propulsion Laboratory
	Modifications: TCSASSEMBLER, TopCoder

	Modification History:
	Date       Who     What
	9-24-13    TC      Added LtpXmitAuthRule and LtpRecvAuthRule
			   structures, added lists of ltpXmitAuthRule
			   and ltpRecvAuthRules in SecDB structure
									*/
#ifndef _SEC_H_
#define _SEC_H_

#include "ion.h"

#define	EPOCH_2000_SEC	946684800

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	char		name[32];	/*	NULL-terminated.	*/
	int		length;
	Object		value;
} SecKey;				/*	Symmetric keys.		*/

typedef struct
{
	BpTimestamp	effectiveTime;
	int		length;
	Object		value;
} OwnPublicKey;

typedef struct
{
	BpTimestamp	effectiveTime;
	int		length;
	Object		value;
} PrivateKey;

typedef struct
{
	uvast		nodeNbr;
	BpTimestamp	effectiveTime;
	time_t		assertionTime;
	int		length;
	Object		value;
} PublicKey;				/*	Not used for Own keys.	*/

typedef struct
{
	uvast		nodeNbr;
	BpTimestamp	effectiveTime;
	Object		publicKeyElt;	/*	Ref. to PublicKey.	*/
} PubKeyRef;				/*	Not used for Own keys.	*/

#ifdef ORIGINAL_BSP
typedef struct
{
	Object  securitySrcEid;		/* 	An sdrstring.	        */
	Object	securityDestEid;	/*	An sdrstring.		*/
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspBabRule;

typedef struct
{
	Object  securitySrcEid;		/* 	An sdrstring.	        */
	Object	securityDestEid;	/*	An sdrstring.		*/
	int	blockTypeNbr;	
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspPibRule;

typedef struct
{
	Object  securitySrcEid;		/* 	An sdrstring.	        */
	Object	securityDestEid;	/*	An sdrstring.		*/
	int	blockTypeNbr;	
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspPcbRule;
#else
typedef struct
{
	Object  senderEid;		/* 	An sdrstring.	        */
	Object	receiverEid;		/*	An sdrstring.		*/
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspBabRule;

typedef struct
{
	Object  securitySrcEid;		/* 	An sdrstring.	        */
	Object	destEid;		/*	An sdrstring.		*/
	int	blockTypeNbr;	
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspBibRule;

typedef struct
{
	Object  securitySrcEid;		/* 	An sdrstring.	        */
	Object	destEid;		/*	An sdrstring.		*/
	int	blockTypeNbr;	
	char	ciphersuiteName[32];	/*	NULL-terminated.	*/
	char	keyName[32];		/*	NULL-terminated.	*/
} BspBcbRule;
#endif

/*		LTP authentication ciphersuite numbers			*/
#define LTP_AUTH_HMAC_SHA1_80	0
#define LTP_AUTH_RSA_SHA256	1
#define LTP_AUTH_NULL		255

/*	LtpXmitAuthRule records an LTP segment signing rule for an
 *	identified remote LTP engine.  The rule specifies the
 *	ciphersuite to use for signing those segments and the
 *	name of the key that the indicated ciphersuite must use.	*/
typedef struct
{
	uvast		ltpEngineId;
	unsigned char	ciphersuiteNbr;
	char		keyName[32];
} LtpXmitAuthRule;

/*	LtpRecvAuthRule records an LTP segment authentication rule
 *	for an identified remote LTP engine.  The rule specifies
 *	the ciphersuite to use for authenticating segments and the
 *	name of the key that the indicated ciphersuite must use.	*/
typedef struct
{
	uvast		ltpEngineId;
	unsigned char	ciphersuiteNbr;
	char		keyName[32];
} LtpRecvAuthRule;

typedef struct
{
	Object	publicKeys;		/*	SdrList PublicKey	*/
	Object	ownPublicKeys;		/*	SdrList OwnPublicKey	*/
	Object	privateKeys;		/*	SdrList PrivateKey	*/
	time_t	nextRekeyTime;		/*	UTC			*/
	Object	keys;			/*	SdrList of SecKey	*/
	Object	bspBabRules;		/*	SdrList of BspBabRule	*/
#ifdef ORIGINAL_BSP
	Object	bspPibRules;		/*	SdrList of BspBibRule	*/
	Object	bspPcbRules;		/*	SdrList of BspBcbRule	*/
#else
	Object	bspBibRules;		/*	SdrList of BspBibRule	*/
	Object	bspBcbRules;		/*	SdrList of BspBcbRule	*/
#endif
	Object	ltpXmitAuthRules;	/*	SdrList LtpXmitAuthRule	*/
	Object	ltpRecvAuthRules;	/*	SdrList LtpRecvAuthRule	*/
} SecDB;

typedef struct
{
	PsmAddress	publicKeys;	/*	SM RB tree of PubKeyRef	*/
} SecVdb;

extern int	secAttach();
extern Object	getSecDbObject();
extern SecVdb	*getSecVdb();

/*	*	Functions for managing public keys.			*/

extern int	sec_get_public_key(uvast nodeNbr, BpTimestamp *effectiveTime,
			int *datBufferLen, unsigned char *datBuffer);
		/*	Retrieves the value of the public key that
		 *	was valid at "effectiveTime" for the node
		 *	identified by "nodeNbr" (which must not be
		 *	the local node).  The value is written into
		 *	datBuffer unless its length exceeds the length
		 *	of the buffer, which must be supplied in
		 *	*datBufferLen.
		 *
		 *	On success, returns the actual length of the
		 *	key.  If *datBufferLen is less than the
		 *	actual length of the key, returns 0 and
		 *	replaces buffer length in *datBufferLen with
		 *	the actual key length.  If the requested
		 *	key is not found, returns 0 and leaves the
		 *	value in *datBufferLen unchanged.  On
		 *	system failure returns -1.			*/

extern int	sec_get_private_key(BpTimestamp *effectiveTime,
			int *datBufferLen, unsigned char *datBuffer);
		/*	Retrieves the value of the private key that was
		 *	valid at "effectiveTime" for the local node.
		 *	The value is written into datBuffer unless
		 *	its length exceeds the length of the buffer,
		 *	which must be supplied in *datBufferLen.
		 *
		 *	On success, returns the actual length of the
		 *	key.  If *datBufferLen is less than the
		 *	actual length of the key, returns 0 and
		 *	replaces buffer length in *datBufferLen
		 *	with the actual key length.  If the
		 *	key is not found, returns 0 and leaves the
		 *	value in *datBufferLen unchanged.  On
		 *	system failure returns -1.			*/

/*	*	Functions for retrieving security information.		*/

extern int	sec_get_key(char *keyName,
			int *keyBufferLength,
			char *keyValueBuffer);
		/*	Retrieves the value of the security key
		 *	identified by "keyName".  The value is
		 *	written into keyValueBuffer unless its
		 *	length exceeds the length of the buffer,
		 *	which must be supplied in *keyBufferLength.
		 *
		 *	On success, returns the actual length of
		 *	key.  If *keyBufferLength is less than the
		 *	actual length of the key, returns 0 and
		 *	replaces buffer length in *keyBufferLength
		 *	with the actual key length.  If the named
		 *	key is not found, returns 0 and leaves the
		 *	value in *keyBufferLength unchanged.  On
		 *	system failure returns -1.			*/

#ifdef __cplusplus
}
#endif

#endif  /* _SEC_H_ */
