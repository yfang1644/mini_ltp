/*
 * ============================================================================
 *
 *       Filename:  ltp.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/07/2021 10:34:02 PM
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

/*
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


#endif
