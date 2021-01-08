/*
 * ============================================================================
 *
 *       Filename:  sms.h
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

#ifndef _SMS_H
#define _SMS_H

/*   Segment Type Code
   #CTRL EXC Flag 1 Flag 0 Code  Nature of segment
   #---- --- ------ ------ ----  ---------------------------------------
     #0   0     0      0     0   Red data, NOT {Checkpoint, EORP or EOB}
     #0   0     0      1     1   Red data, Checkpoint, NOT {EORP or EOB}
     #0   0     1      0     2   Red data, Checkpoint, EORP, NOT EOB
     #0   0     1      1     3   Red data, Checkpoint, EORP, EOB

     #0   1     0      0     4   Green data, NOT EOB
     #0   1     0      1     5   Green data, undefined
     #0   1     1      0     6   Green data, undefined
     #0   1     1      1     7   Green data, EOB

     #1   0     0      0     8   Report segment
     #1   0     0      1     9   Report-acknowledgment segment
     #1   0     1      0    10   Control segment, undefined
     #1   0     1      1    11   Control segment, undefined

     #1   1     0      0    12   Cancel segment from block sender
     #1   1     0      1    13   Cancel-acknowledgment segment
                                #to block sender

     #1   1     1      0    14   Cancel segment from block receiver
     #1   1     1      1    15   Cancel-acknowledgment segment
                                #to block receiver
*/

typedef struct sms_header {
    unsigned char version:4;
    unsigned char type:4;
    unsigned int sessionId;     /* pick uid() as session number */
                                /* and pid() as session number  */
    unsigned char hecnt:4;
    unsigned char tecnt:4;
    unsigned char extensions[4];
}
#endif
