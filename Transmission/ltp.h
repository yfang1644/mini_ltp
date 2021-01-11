/*
 * ============================================================================
 *
 *       Filename:  ltp.h
 *
 *    Description:  Session Management segment
 *
 *        Version:  1.0
 *        Created:  01/08/2021 02:40:52 PM
 *       Revision:  none
 *       Compiler: 
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#ifndef _LTP_H
#define _LTP_H

typedef struct {
    unsigned int 
}ltpSessionID;

/*
 * Segment Type Code
 CTRL EXC Flag1 Flag0 Code  Nature of segment
 ---- --- ----- ----- ----  ---------------------------------------
  0    0    0     0     0   Red data, NOT {Checkpoint, EORP or EOB}
  0    0    0     1     1   Red data, Checkpoint, NOT {EORP or EOB}
  0    0    1     0     2   Red data, Checkpoint, EORP, NOT EOB
  0    0    1     1     3   Red data, Checkpoint, EORP, EOB
 
  0    1    0     0     4   Green data, NOT EOB
  0    1    0     1     5   Green data, undefined
  0    1    1     0     6   Green data, undefined
  0    1    1     1     7   Green data, EOB
 
  1    0    0     0     8   Report segment
  1    0    0     1     9   Report-acknowledgment segment
  1    0    1     0    10   Control segment, undefined
  1    0    1     1    11   Control segment, undefined
 
  1    1    0     0    12   Cancel segment from block sender
  1    1    0     1    13   Cancel-acknowledgment segment to block sender
  1    1    1     0    14   Cancel segment from block receiver
  1    1    1     1    15   Cancel-acknowledgment segment to block receiver

   Segment Class Masks
  ---------------------------
  0    0    -     1     CP   Checkpoint
  0    0    1     -     CP   Checkpoint
  0    0    1     -     EORP End of red-part; red-part size = offset + length
  0    -    1     1     EOB  End of block; block size = offset + length
  1    0    0     0     RS   Report segment; carries reception claims
  1    0    0     1     RA   Report-acknowledgment segment
  1    1    0     0     CS   Cancel segment from block sender
  1    1    0     1     CAS  Cancel-acknowledgment segment to block sender
  1    1    1     0     CR   Cancel segment from block receiver
  1    1    1     1     CAR  Cancel-acknowledgment segment to block receiver
  1    1    -     0     Cx   Cancel segment (generic)
  1    1    -     1     CAx  Cancel-acknowledgment segment (generic)
*/

/*
    Extension tag   Meanin 
    -------------   ------- 
    0x00            LTP authentication extension
    0x01            LTP cookie extension
    0x02-0xAF       Unassigned
    0xB0-0xBF       Reserved
    0xC0-0xFF       Private / Experimental Use
*/

/* extension in TLV (type-length-value) */
typedef struct extension {
    unsigned char tag;
    unsigned char *length;
    unsigned char *value;
} Sms_ext;

typedef struct sms_header {
    unsigned char version:4;
    unsigned char type:4;
    unsigned int sessionId;     /* pick uid() as session number */
                                /* and pid() as session number  */
    unsigned char hecnt:4;
    unsigned char tecnt:4;
    Sms_ext *ext;
} Sms_header;


#endif
