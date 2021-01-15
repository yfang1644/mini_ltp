/*

	ionsec.c:	API for managing ION's security database.

	Copyright (c) 2009, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.

	Author:		Scott Burleigh, JPL
	Modifications:	TCSASSEMBLER, TopCoder

	Modification History:
	Date       Who     What
	9-24-13    TC      Added functions (find, add, remove, change) to
			   manage ltpXmitAuthRule and ltpRecvAuthRule
			   Updated secInitialize to initialize SecDB's 
			   ltpXmitAuthRule and ltpRecvAuthRule lists
			   Added writeRuleMessage to print rule-related message
	11-15-13  romanoTC Check for valid ciphersuite values (0,1,255)

									*/
#include "ionsec.h"

static Object	_secdbObject(Object *newDbObj)
{
	static Object	obj = 0;
	
	if (newDbObj)
	{
		obj = *newDbObj;
	}
	
	return obj;
}

static SecDB	*_secConstants()
{
	static SecDB	buf;
	static SecDB	*db = NULL;
	Sdr		sdr;
	Object		dbObject;

	if (db == NULL)
	{
		sdr = getIonsdr();
		CHKNULL(sdr);
		dbObject = _secdbObject(0);
		if (dbObject)
		{
			if (sdr_heap_is_halted(sdr))
			{
				sdr_read(sdr, (char *) &buf, dbObject,
						sizeof(SecDB));
			}
			else
			{
				CHKNULL(sdr_begin_xn(sdr));
				sdr_read(sdr, (char *) &buf, dbObject,
						sizeof(SecDB));
				sdr_exit_xn(sdr);
			}

			db = &buf;
		}
	}

	return db;
}

static int	orderKeyRefs(PsmPartition wm, PsmAddress refData,
			void *dataBuffer)
{
	PubKeyRef	*ref;
	PubKeyRef	*argRef;

	ref = (PubKeyRef *) psp(wm, refData);
	argRef = (PubKeyRef *) dataBuffer;
	if (ref->nodeNbr < argRef->nodeNbr)
	{
		return -1;
	}

	if (ref->nodeNbr > argRef->nodeNbr)
	{
		return 1;
	}

	/*	Matching node number.					*/

	if (ref->effectiveTime.seconds < argRef->effectiveTime.seconds)
	{
		return -1;
	}

	if (ref->effectiveTime.seconds > argRef->effectiveTime.seconds)
	{
		return 1;
	}

	if (ref->effectiveTime.count < argRef->effectiveTime.count)
	{
		return -1;
	}

	if (ref->effectiveTime.count > argRef->effectiveTime.count)
	{
		return 1;
	}

	/*	Matching effective time.				*/

	return 0;
}

static void	eraseKeyRef(PsmPartition wm, PsmAddress refData, void *arg)
{
	psm_free(wm, refData);
}

static int	loadPublicKey(PsmPartition wm, PsmAddress rbt, PublicKey *key,
			Object elt)
{
	PsmAddress	refAddr;
	PubKeyRef	*ref;

	refAddr = psm_zalloc(wm, sizeof(PubKeyRef));
	if (refAddr == 0)
	{
		return -1;
	}

	ref = (PubKeyRef *) psp(wm, refAddr);
	CHKERR(ref);
	ref->nodeNbr = key->nodeNbr;
	ref->effectiveTime.seconds = key->effectiveTime.seconds;
	ref->effectiveTime.count = key->effectiveTime.count;
	ref->publicKeyElt = elt;
	if (sm_rbt_insert(wm, rbt, refAddr, orderKeyRefs, ref) == 0)
	{
		psm_free(wm, refAddr);
		return -1;
	}

	return 0;
}

static int	loadPublicKeys(PsmAddress rbt)
{
	PsmPartition	wm = getIonwm();
	Sdr		sdr = getIonsdr();
	SecDB		db;
	Object		elt;
			OBJ_POINTER(PublicKey, nodeKey);

	sdr_read(sdr, (char *) &db, _secdbObject(NULL), sizeof(SecDB));
	for (elt = sdr_list_first(sdr, db.publicKeys); elt;
			elt = sdr_list_next(sdr, elt))
	{
		GET_OBJ_POINTER(sdr, PublicKey, nodeKey,
				sdr_list_data(sdr, elt));
		if (loadPublicKey(wm, rbt, nodeKey, elt) < 0)
		{
			putErrmsg("Can't add public key reference.", NULL);
			return -1;
		}
	}

	return 0;
}

static SecVdb	*_secvdb(char **name)
{
	static SecVdb	*vdb = NULL;
	PsmPartition	wm;
	PsmAddress	vdbAddress;
	PsmAddress	elt;
	Sdr		sdr;

	if (name)
	{
		if (*name == NULL)	/*	Terminating.		*/
		{
			vdb = NULL;
			return vdb;
		}

		/*	Attaching to volatile database.			*/

		wm = getIonwm();
		if (psm_locate(wm, *name, &vdbAddress, &elt) < 0)
		{
			putErrmsg("Failed searching for vdb.", NULL);
			return vdb;
		}

		if (elt)
		{
			vdb = (SecVdb *) psp(wm, vdbAddress);
			return vdb;
		}

		/*	Security volatile database doesn't exist yet.	*/

		sdr = getIonsdr();
		CHKNULL(sdr_begin_xn(sdr));	/*	To lock memory.	*/

		/*	Create and catalogue the SecVdb object.	*/

		vdbAddress = psm_zalloc(wm, sizeof(SecVdb));
		if (vdbAddress == 0)
		{
			putErrmsg("No space for volatile database.", NULL);
			sdr_exit_xn(sdr);
			return NULL;
		}

		if (psm_catlg(wm, *name, vdbAddress) < 0)
		{
			putErrmsg("Can't catalogue volatile database.", NULL);
			psm_free(wm, vdbAddress);
			sdr_exit_xn(sdr);
			return NULL;
		}

		vdb = (SecVdb *) psp(wm, vdbAddress);
		vdb->publicKeys = sm_rbt_create(wm);
		if (vdb->publicKeys == 0)
		{
			putErrmsg("Can't initialize volatile database.", NULL);
			vdb = NULL;
			oK(psm_uncatlg(wm, *name));
			psm_free(wm, vdbAddress);
			sdr_exit_xn(sdr);
			return NULL;
		}

		if (loadPublicKeys(vdb->publicKeys) < 0)
		{
			putErrmsg("Can't load volatile database.", NULL);
			oK(sm_rbt_destroy(wm, vdb->publicKeys, eraseKeyRef,
					NULL));
			vdb = NULL;
			oK(psm_uncatlg(wm, *name));
			psm_free(wm, vdbAddress);
		}

		sdr_exit_xn(sdr);	/*	Unlock memory.		*/
	}

	return vdb;
}


int	secAttach()
{
	Sdr	ionsdr;
	Object	secdbObject;
	SecVdb	*secvdb = _secvdb(NULL);
	char	*secvdbName = "secvdb";

	if (ionAttach() < 0)
	{
		putErrmsg("Bundle security can't attach to ION.", NULL);
		return -1;
	}

	ionsdr = getIonsdr();
	secdbObject = _secdbObject(NULL);
	if (secdbObject == 0)
	{
		CHKERR(sdr_begin_xn(ionsdr));
		secdbObject = sdr_find(ionsdr, "secdb", NULL);
		sdr_exit_xn(ionsdr);
		if (secdbObject == 0)
		{
			writeMemo("[?] Can't find ION security database.");
			return -1;
		}

		oK(_secdbObject(&secdbObject));
	}

	oK(_secConstants());
	if (secvdb == NULL)
	{
		if (_secvdb(&secvdbName) == NULL)
		{
			putErrmsg("Can't initialize ION security vdb.", NULL);
			return -1;
		}
	}

	return 0;
}

Object	getSecDbObject()
{
	return _secdbObject(NULL);
}

SecVdb	*getSecVdb()
{
	return _secvdb(NULL);
}

static Object	locatePrivateKey(BpTimestamp *effectiveTime, Object *nextKey)
{
	Sdr	sdr = getIonsdr();
	SecDB	*secdb = _secConstants();
	Object	elt;
		OBJ_POINTER(PrivateKey, key);

	/*	This function locates the PrivateKey identified by
	 *	effectiveTime, if any.  If none, notes the location
	 *	within the list of private keys at which such
	 *	a key should be inserted.				*/

	CHKZERO(ionLocked());
	if (nextKey) *nextKey = 0;	/*	Default.		*/
	if (secdb == NULL)	/*	No security database declared.	*/
	{
		return 0;
	}

	for (elt = sdr_list_first(sdr, secdb->privateKeys); elt;
			elt = sdr_list_next(sdr, elt))
	{
		GET_OBJ_POINTER(sdr, PrivateKey, key, sdr_list_data(sdr, elt));
		if (key->effectiveTime.seconds < effectiveTime->seconds)
		{
			continue;
		}

		if (key->effectiveTime.seconds > effectiveTime->seconds)
		{
			if (nextKey) *nextKey = elt;
			break;		/*	Same as end of list.	*/
		}

		if (key->effectiveTime.count < effectiveTime->count)
		{
			continue;
		}

		if (key->effectiveTime.count > effectiveTime->count)
		{
			if (nextKey) *nextKey = elt;
			break;		/*	Same as end of list.	*/
		}

		return elt;
	}

	return 0;
}


int	sec_get_public_key(uvast nodeNbr, BpTimestamp *effectiveTime,
		int *keyBufferLen, unsigned char *keyValueBuffer)
{
	Sdr		sdr = getIonsdr();
	SecDB		*secdb = _secConstants();
	PsmPartition	wm = getIonwm();
	SecVdb		*vdb = getSecVdb();
	PubKeyRef	argRef;
	PsmAddress	rbtNode;
	PsmAddress	successor;
	PsmAddress	refAddr;
	PubKeyRef	*ref;
	Object		keyObj;
	PublicKey	publicKey;

	if (secdb == NULL)	/*	No security database declared.	*/
	{
		return 0;
	}

	CHKERR(vdb);
	CHKERR(effectiveTime);
	CHKERR(keyBufferLen);
	CHKERR(*keyBufferLen > 0);
	CHKERR(keyValueBuffer);
	argRef.nodeNbr = nodeNbr;
	argRef.effectiveTime.seconds = effectiveTime->seconds;
	argRef.effectiveTime.count = effectiveTime->count;
	CHKERR(sdr_begin_xn(sdr));
	rbtNode = sm_rbt_search(wm, vdb->publicKeys, orderKeyRefs, &argRef,
			&successor);
	if (rbtNode == 0)	/*	No exact match (normal).	*/
	{
		if (successor == 0)
		{
			rbtNode = sm_rbt_last(wm, vdb->publicKeys);
		}
		else
		{
			rbtNode = sm_rbt_prev(wm, successor);
		}

		if (rbtNode == 0)
		{
			sdr_exit_xn(sdr);
			return 0;	/*	No such key.		*/
		}
	}

	refAddr = sm_rbt_data(wm, rbtNode);
	ref = (PubKeyRef *) psp(wm, refAddr);
	if (ref->nodeNbr != nodeNbr)
	{
		sdr_exit_xn(sdr);
		return 0;		/*	No such key.		*/
	}

	/*	Ref now points to the last-effective public key for
	 *	this node that was in effect at a time at or before
	 *	the indicated effective time.				*/

	keyObj = sdr_list_data(sdr, ref->publicKeyElt);
	sdr_read(sdr, (char *) &publicKey, keyObj, sizeof(PublicKey));
	if (publicKey.length > *keyBufferLen)
	{
		/*	Buffer is too small for this key value.		*/

		sdr_exit_xn(sdr);
		*keyBufferLen = publicKey.length;
		return 0;
	}

	sdr_read(sdr, (char *) keyValueBuffer, publicKey.value,
			publicKey.length);
	sdr_exit_xn(sdr);
	return publicKey.length;
}


int	sec_get_private_key(BpTimestamp *effectiveTime, int *keyBufferLen,
		unsigned char *keyValueBuffer)
{
	Sdr		sdr = getIonsdr();
	SecDB		*secdb = _secConstants();
	Object		keyElt;
	Object		nextKey;
	Object		keyObj;
	PrivateKey	privateKey;

	if (secdb == NULL)	/*	No security database declared.	*/
	{
		return 0;
	}

	CHKERR(effectiveTime);
	CHKERR(keyBufferLen);
	CHKERR(*keyBufferLen > 0);
	CHKERR(keyValueBuffer);
	keyElt = locatePrivateKey(effectiveTime, &nextKey);
	if (keyElt == 0)	/*	No exact match (normal).	*/
	{
		if (nextKey == 0)
		{
			keyElt = sdr_list_last(sdr, secdb->privateKeys);
		}
		else
		{
			keyElt = sdr_list_prev(sdr, nextKey);
		}

		if (keyElt == 0)
		{
			sdr_exit_xn(sdr);
			return 0;	/*	No such key.		*/
		}
	}

	/*	keyElt now points to the last-effective private key
	 *	for the local node that was in effect at a time at
	 *	or before the indicated effective time.			*/

	keyObj = sdr_list_data(sdr, keyElt);
	sdr_read(sdr, (char *) &privateKey, keyObj, sizeof(PrivateKey));
	if (privateKey.length > *keyBufferLen)
	{
		/*	Buffer is too small for this key value.		*/

		sdr_exit_xn(sdr);
		*keyBufferLen = privateKey.length;
		return 0;
	}

	sdr_read(sdr, (char *) keyValueBuffer, privateKey.value,
			privateKey.length);
	sdr_exit_xn(sdr);
	return privateKey.length;
}

static Object	locateKey(char *keyName, Object *nextKey)
{
	Sdr	sdr = getIonsdr();
	SecDB	*secdb = _secConstants();
	Object	elt;
		OBJ_POINTER(SecKey, key);
	int	result;

	/*	This function locates the SecKey identified by the
	 *	specified name, if any.  If none, notes the
	 *	location within the keys list at which such a key
	 *	should be inserted.					*/

	CHKZERO(ionLocked());
	if (nextKey) *nextKey = 0;	/*	Default.		*/
	if (secdb == NULL)	/*	No security database declared.	*/
	{
		return 0;
	}

	for (elt = sdr_list_first(sdr, secdb->keys); elt;
			elt = sdr_list_next(sdr, elt))
	{
		GET_OBJ_POINTER(sdr, SecKey, key, sdr_list_data(sdr, elt));
		result = strcmp(key->name, keyName);
		if (result < 0)
		{
			continue;
		}

		if (result > 0)
		{
			if (nextKey) *nextKey = elt;
			break;		/*	Same as end of list.	*/
		}

		return elt;
	}

	return 0;
}

void	sec_findKey(char *keyName, Object *keyAddr, Object *eltp)
{
	Sdr	sdr = getIonsdr();
	Object	elt;

	/*	This function finds the SecKey for the specified
	 *	node, if any.						*/

	CHKVOID(keyName);
	CHKVOID(keyAddr);
	CHKVOID(eltp);
	*eltp = 0;
	CHKVOID(sdr_begin_xn(sdr));
	elt = locateKey(keyName, NULL);
	if (elt == 0)
	{
		sdr_exit_xn(sdr);
		return;
	}

	*keyAddr = sdr_list_data(sdr, elt);
	sdr_exit_xn(sdr);
	*eltp = elt;
}


int	sec_get_key(char *keyName, int *keyBufferLength, char *keyValueBuffer)
{
	Sdr	sdr = getIonsdr();
	Object	keyAddr;
	Object	elt;
		OBJ_POINTER(SecKey, key);

	CHKERR(keyName);
	CHKERR(keyBufferLength);
	CHKERR(keyValueBuffer);
	CHKERR(sdr_begin_xn(sdr));
	sec_findKey(keyName, &keyAddr, &elt);
	if (elt == 0)
	{
		sdr_exit_xn(sdr);
		return 0;
	}

	GET_OBJ_POINTER(sdr, SecKey, key, keyAddr);
	if (key->length > *keyBufferLength)
	{
		sdr_exit_xn(sdr);
		*keyBufferLength = key->length;
		return 0;
	}

	sdr_read(sdr, keyValueBuffer, key->value, key->length);
	sdr_exit_xn(sdr);
	return key->length;
}

