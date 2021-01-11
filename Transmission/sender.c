/*
 * ============================================================================
 *
 *       Filename:  sender.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/09/2021 05:25:56 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

void ltpSender()
{
    // parse packet
    if (RP == 0)
    {
        full_GP_transmission();
    }
    else{
        RP_transmission();
    }
}
