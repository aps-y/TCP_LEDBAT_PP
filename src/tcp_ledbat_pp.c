#include <linux/module.h>
#include <net/tcp.h>
#include <linux/vmalloc.h>


#define  DEBUG_SLOW_START    0
#define  DEBUG_SLOWDOWN	     0
#define  DEBUG_NORMAL	     0
#define  DEBUG_DELAY         0
#define  DEBUG_NOISE_FILTER  0
#define  DEBUG_BASE_HISTO    0

/* NOTE: len are the actual length - 1 */
static int base_histo_len = 10;
static int noise_filter_len = 4;
static int target = 100;
static int do_ss = 1;
static int ledbat_ssthresh = 0xffff;
static int ledbat_pp_ai_const_num =1;
static int ledbat_pp_ai_const_den =1;
static int min_cwnd = 2;

module_param(base_histo_len, int, 0644);
MODULE_PARM_DESC(base_histo_len, "length of the base history vector");
module_param(noise_filter_len, int, 0644);
MODULE_PARM_DESC(noise_filter_len, "length of the noise_filter vector");
module_param(target, int, 0644);
MODULE_PARM_DESC(target, "target queuing delay");

/* Our extensions to play with slow start */
#define DO_NOT_SLOWSTART            0
#define DO_SLOWSTART                1
#define DO_SLOWSTART_WITH_THRESHOLD 2

module_param(do_ss, int, 0644);
MODULE_PARM_DESC(do_ss, "do slow start: 0 no, 1 yes, 2 with_ssthresh");
module_param(ledbat_ssthresh, int, 0644);
MODULE_PARM_DESC(ledbat_ssthresh, "slow start threshold");
module_param(ledbat_pp_ai_const_num, int, 0644);									//Constants for calculating cwnd
MODULE_PARM_DESC(ledbat_pp_ai_const_num, "constant value numerator");				//
module_param(ledbat_pp_ai_const_den, int, 0644);									//
MODULE_PARM_DESC(ledbat_pp_ai_const_num, "constant value denominator");				//
module_param(min_cwnd, int, 0644);													//
MODULE_PARM_DESC(min_cwnd, "minimum cwnd value");

struct owd_circ_buf {
	u32 *buffer;
	u8 first;
	u8 next;
	u8 len;
	u8 min;
};

/*
 * TCP-LEDBAT's state flags.
 * We create this set of state flags mainly for debugging.
 */
enum tcp_ledbat_state {
	LEDBAT_VALID_OWD = (1 << 1),
	LEDBAT_INCREASING = (1 << 2),
	LEDBAT_CAN_SS = (1 << 3),
    LEDBAT_SLOWDOWN = (1 << 4),
};

/**
 * struct ledbat
 */
struct ledbat {
	u32 last_rollover;
	u32 snd_cwnd_cnt;	/* already in struct tcp_sock but we need 32 bits. */
	u32 last_ack;       
    u32 next_slowdown;  // Time when the next slowdown will begin
    u32 slowdown_start; // Time when last/current slowdown started
    bool first_ss;      // flag to keep track of 1st slowstart
	struct owd_circ_buf base_history;
	struct owd_circ_buf noise_filter;
	u32 flag;
};

static int ledbat_init_circbuf(struct owd_circ_buf *buffer, u16 len)
{
	u32 *b = kmalloc(len * sizeof(u32), GFP_KERNEL);
	if (b == NULL)
		return 1;
	buffer->len = len;
	buffer->buffer = b;
	buffer->first = 0;
	buffer->next = 0;
	buffer->min = 0;
	return 0;
}

static void tcp_ledbat_release(struct sock *sk)
{
	struct ledbat *ledbat = inet_csk_ca(sk);
	kfree(ledbat->noise_filter.buffer);
	kfree(ledbat->base_history.buffer);
}

/**
 * tcp_ledbat_init
 */
static void tcp_ledbat_init(struct sock *sk)
{
	struct ledbat *ledbat = inet_csk_ca(sk);

	ledbat_init_circbuf(&(ledbat->base_history), base_histo_len + 1);
	ledbat_init_circbuf(&(ledbat->noise_filter), noise_filter_len + 1);

	ledbat->last_rollover = 0;
	ledbat->flag = 0;
	ledbat->snd_cwnd_cnt = 0;
    ledbat->first_ss = true;
	ledbat->last_ack = 0;
    ledbat->next_slowdown = 0xffffffff;
    ledbat->slowdown_start = 0;

	if (do_ss) {
		ledbat->flag |= LEDBAT_CAN_SS;
	}
	ledbat->flag |= LEDBAT_VALID_OWD;

}

typedef u32(*ledbat_filter_function) (struct owd_circ_buf *);

static u32 ledbat_min_circ_buff(struct owd_circ_buf *b)
{
	/* 
	   The draft requires all history to be set to +infinity on initialization. 
	   We obtain the same behavior returning +infinity in case of empty history.
	 */
	if (b->first == b->next)
		return 0xffffffff;
	return b->buffer[b->min];
}

static
u32 ledbat_current_delay(struct ledbat *ledbat, ledbat_filter_function filter)
{
	return filter(&(ledbat->noise_filter));
}

static u32 ledbat_base_delay(struct ledbat *ledbat)
{
	return ledbat_min_circ_buff(&(ledbat->base_history));
}

#if DEBUG_NOISE_FILTER || DEBUG_BASE_HISTO
static void print_delay(struct owd_circ_buf *cb, char *name)
{
	u16 curr = cb->first;
	printk(KERN_DEBUG "LEDBAT: %s: time %u ", name, tcp_jiffies32);

	while (curr != cb->next) {
		printk(KERN_DEBUG "LEDBAT: %u ", cb->buffer[curr]);
		curr = (curr + 1) % cb->len;
	}

	printk(KERN_DEBUG "LEDBAT: min %u, len %u, first %u, next %u\n",
	       cb->buffer[cb->min], cb->len, cb->first, cb->next);
}
#endif

static
u32 tcp_ledbat_ssthresh(struct sock *sk)
{
	u32 res;
	switch (do_ss) {
	case DO_NOT_SLOWSTART:
	case DO_SLOWSTART:
	default:
		res = tcp_reno_ssthresh(sk);
		break;
	case DO_SLOWSTART_WITH_THRESHOLD:
		res = ledbat_ssthresh;
		break;
	}

	return res;
}

/**
 * tcp_ledbat_slow_start
 */

u32 tcp_ledbat_slow_start(struct tcp_sock *tp, u32 acked, u32 gain_den)
{
	u32 cwnd;
	u32 inc;
	inc = gain_den*(tp->snd_ssthresh - tp->snd_cwnd);
	
	if(acked>=inc)
	{
		acked-=inc;
		cwnd= tp->snd_ssthresh;
	}

	else{
		inc = do_div(acked,gain_den);
		if(inc)
		acked++;
		cwnd= tp->snd_cwnd + acked;
		acked=0;
	}
	
	
	tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);

	return acked;
}


/**
 * tcp_ledbat_cong_avoid
 */
static void tcp_ledbat_cong_avoid(struct sock *sk, u32 ack, u32 acked)
// ns2 version takes also this two parameters: u32 rtt, int flag
{

	struct ledbat *ledbat = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	s64 queue_delay;
	s64 offset;
	s64 cwnd;
	//u32 rem;
	u32 gain_den =4;
	u32 max_cwnd;
	u32 rtt;
	s64 current_delay;
	s64 base_delay;


	
	/*if no valid data return */
	if (!(ledbat->flag & LEDBAT_VALID_OWD))
		return;

	/* calculating gain*/                                                      //This piece code was not working fine
	// gain_den = 2*target;
    // rem = do_div(gain_den, base_delay);
	// if(rem)
	// gain_den++;
	// if(gain_den>16)
	// gain_den=16;
	// if(gain_den==0)
	// gain_den=1;

	rtt = (tp->srtt_us >> 3)/(USEC_PER_SEC/TCP_TS_HZ);
	max_cwnd = ((u32) (tp->snd_cwnd)) * target * gain_den * ledbat_pp_ai_const_den;   

	/* 
	   This checks that we are not limited by the congestion window nor by the
	   application, and is basically the same check that it is performed in the
	   draft.
	 */
	if (!tcp_is_cwnd_limited(sk))
		return;


	// if TCP is in slowdown phase then set cwnd = 2

    if(ledbat->flag & LEDBAT_SLOWDOWN && (!after(tcp_time_stamp(tp), ledbat->slowdown_start + 2*rtt)))
    {
        tp->snd_cwnd = min_cwnd;
		#if DEBUG_SLOWDOWN
		printk(KERN_DEBUG
		       "LEDBAT: 2) SLOWDOWN!!! clamp %d cwnd %d sshthresh %d \n",
		       tp->snd_cwnd_clamp, tp->snd_cwnd, tp->snd_ssthresh);
		#endif
        return;
    }

	if (tp->snd_cwnd <= min_cwnd)
		ledbat->flag |= LEDBAT_CAN_SS;

	if (do_ss >= DO_SLOWSTART && tcp_in_slow_start(tp) &&
	    (ledbat->flag & LEDBAT_CAN_SS)) {
#if DEBUG_SLOW_START
		
		printk(KERN_DEBUG
		       "LEDBAT: 1) slow_start!!! clamp %d cwnd %d sshthresh %d\n",
		       tp->snd_cwnd_clamp, tp->snd_cwnd, tp->snd_ssthresh);
#endif
		acked = tcp_ledbat_slow_start(tp, acked, gain_den);
		if (!acked)
			return;
	} else {
		ledbat->flag &= ~LEDBAT_CAN_SS;
	}
	

    /**************** slowdown phase ***********/

	//First slowstart is over so calculating time of first slowdown
    if(ledbat->first_ss)
    {
        ledbat->next_slowdown = tcp_time_stamp(tp) + 2*(rtt);
        ledbat->first_ss=false;
		#if DEBUG_SLOW_START
		printk(KERN_DEBUG
			   "LEDBAT: 1) FIRST SLOWSTART - slowdown_start %u cwnd %d next_slowdown %u \n",
			   ledbat->slowdown_start, tp->snd_cwnd, ledbat->next_slowdown);
		#endif
    }

	//Updating time for next slowdown and leaving slowdown phase
    if( ledbat->flag & LEDBAT_SLOWDOWN)
    {
        ledbat->next_slowdown = tcp_time_stamp(tp) +9*(tcp_time_stamp(tp) - ledbat->slowdown_start);
		ledbat->flag &= ~LEDBAT_SLOWDOWN;
		#if DEBUG_SLOWDOWN
		printk(KERN_DEBUG
			   "LEDBAT: 1)LEDBAT UPDATING next slowdown - slowdown_start %u cwnd %d next_slowdown %u \n",
			   ledbat->slowdown_start, tp->snd_cwnd, ledbat->next_slowdown);
		#endif
	}

	//Entering next slowdown phase and updating ssthresh
    if((after(tcp_time_stamp(tp), ledbat->next_slowdown)))
    {
        ledbat->slowdown_start = tcp_time_stamp(tp);
        ledbat->flag |= LEDBAT_SLOWDOWN;
        tp->snd_ssthresh = tp->snd_cwnd;
        tp->snd_cwnd = min_cwnd;
		#if DEBUG_SLOWDOWN
		printk(KERN_DEBUG
			   "LEDBAT: 1) ENTERING slowdown - slowdown_start %u cwnd %d ss_thresh %d \n",
			   ledbat->slowdown_start, tp->snd_cwnd, tp->snd_ssthresh);
		#endif	   
        return;
    }

    /**************** slowdown phase over***********/
	/* This allows to eventually define new filters for the current delay. */
	current_delay = ((s64) ledbat_current_delay(ledbat, &ledbat_min_circ_buff));
	base_delay = ((s64) ledbat_base_delay(ledbat));	
	queue_delay = current_delay - base_delay;
    if(queue_delay <= target)
    {
        offset = target * ledbat_pp_ai_const_den;
    }
    else{
        offset = ((s64) target) - (queue_delay);
        offset*= ledbat_pp_ai_const_num * tp->snd_cwnd;
        offset+= (target * ledbat_pp_ai_const_den);
		offset = max(offset, (-1 * ((s64)(tp->snd_cwnd >> 1))* gain_den* target * ledbat_pp_ai_const_den));
    }
	/* Do not ramp more than TCP. */
	if (offset > target * gain_den * ledbat_pp_ai_const_den)
		offset = target * gain_den * ledbat_pp_ai_const_den;

#if DEBUG_DELAY
	printk(KERN_DEBUG
	       "LEDBAT: 5) DELAY - time %u, queue_delay %lld, offset %lld cwnd_cnt %u, "
	       "cwnd %u, delay %lld, min %lld\n",
	       tcp_time_stamp(tp), queue_delay, offset, ledbat->snd_cwnd_cnt,
	       tp->snd_cwnd, current_delay, base_delay);
#endif

	/* calculate the new cwnd_cnt */
	cwnd = ledbat->snd_cwnd_cnt + offset*acked;
	if (cwnd > 0) {
		/* if we have a positive number update the cwnd_count */
		ledbat->snd_cwnd_cnt = cwnd;
		if (ledbat->snd_cwnd_cnt >= max_cwnd) {
			/* increase the cwnd */
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
			ledbat->snd_cwnd_cnt = 0;
		}
	} else {
		/* we need to decrease the cwnd but we do not want to set it to 0! */
		if (tp->snd_cwnd > min_cwnd) {
			tp->snd_cwnd--;
			/* set the cwnd_cnt to the max value - target */
			ledbat->snd_cwnd_cnt = (tp->snd_cwnd - 1) * target * gain_den * ledbat_pp_ai_const_den;
		} else {
			tp->snd_cwnd_cnt = 0;
		}
	}
	#if DEBUG_NORMAL
		printk(KERN_DEBUG
		       "LEDBAT: 3) NORMAL!!! clamp %d cwnd %d sshthresh %d \n",
		       tp->snd_cwnd_clamp, tp->snd_cwnd, tp->snd_ssthresh);
	#endif

}

static void ledbat_add_delay(struct owd_circ_buf *cb, u32 owd)
{
	u8 i;

	if (cb->next == cb->first) {
		/*buffer is empty */
		cb->buffer[cb->next] = owd;
		cb->min = cb->next;
		cb->next++;
		return;
	}

	/*set the new delay */
	cb->buffer[cb->next] = owd;
	/* update the min if it is the case */
	if (owd < cb->buffer[cb->min])
		cb->min = cb->next;

	/* increment the next pointer */
	cb->next = (cb->next + 1) % cb->len;

	if (cb->next == cb->first) {
		/* Discard the first element */
		if (cb->min == cb->first) {
			/* Discard the min, search a new one */
			cb->min = i = (cb->first + 1) % cb->len;
			while (i != cb->next) {
				if (cb->buffer[i] < cb->buffer[cb->min])
					cb->min = i;
				i = (i + 1) % cb->len;
			}
		}
		/* move the first */
		cb->first = (cb->first + 1) % cb->len;
	}
}

static void ledbat_update_current_delay(struct ledbat *ledbat, u32 owd)
{
	ledbat_add_delay(&(ledbat->noise_filter), owd);
#if DEBUG_NOISE_FILTER
	printk(KERN_DEBUG "LEDBAT: added delay to noisefilter %u\n", owd);
	print_delay(&(ledbat->noise_filter), "noise_filter");
#endif
}

static void ledbat_update_base_delay(struct ledbat *ledbat, u32 owd)
{
	u32 last;
	struct owd_circ_buf *cb = &(ledbat->base_history);

	if (ledbat->base_history.next == ledbat->base_history.first) {
		/* empty circular buffer */
		ledbat_add_delay(cb, owd);
		return;
	}

	if (after(tcp_jiffies32, ledbat->last_rollover + 60 * HZ )) {
		/* we have finished a minute */
#if DEBUG_BASE_HISTO
		printk(KERN_DEBUG "LEDBAT: time %u, new rollover \n", tcp_jiffies32);
#endif
		ledbat->last_rollover = tcp_jiffies32;
		ledbat_add_delay(cb, owd);
	} else {
		/* update the last value and the min if it is the case */
		last = (cb->next + cb->len - 1) % cb->len;

		if (owd < cb->buffer[last]) {
			cb->buffer[last] = owd;
			if (owd < cb->buffer[cb->min])
				cb->min = last;
		}

	}
#if DEBUG_BASE_HISTO
	printk(KERN_DEBUG "LEDBAT: added delay to base_history %s", "\n");
	print_delay(&(ledbat->base_history), "base_history");
#endif
}

/**
 * tcp_ledbat_rtt_sample
 *
 * - calculate the owd
 * - add the delay to noise filter
 * - if new minute add the delay to base delay or update last delay 
 */
static void tcp_ledbat_rtt_sample(struct sock *sk, u32 rtt)
{
	struct ledbat *ledbat = inet_csk_ca(sk);
	/* halve and covert to msec for a crude estimate of owd */
	u32 mowd = rtt/USEC_PER_MSEC/2;

	/* sorry that we don't have valid data */
	if (!(ledbat->flag & LEDBAT_VALID_OWD)) {
		return;
	}

	ledbat_update_current_delay(ledbat, mowd);
	ledbat_update_base_delay(ledbat, mowd);
}

/**
 * tcp_ledbat_pkts_acked
 */
static void tcp_ledbat_pkts_acked(struct sock *sk,
				  const struct ack_sample *sample)
{
	struct ledbat *ledbat = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (sample->rtt_us > 0)
		tcp_ledbat_rtt_sample(sk, sample->rtt_us);

	if (ledbat->last_ack == 0)
		ledbat->last_ack = tcp_time_stamp(tp);
	else if (after(tcp_time_stamp(tp), ledbat->last_ack + (tp->srtt_us >> 3)/(USEC_PER_SEC/TCP_TS_HZ))) {
		/* we haven't received an acknowledgement for more than a rtt.
		   Set the congestion window to 1. */
		   #if DEBUG_SLOW_START
	        printk(KERN_DEBUG "LEDBAT:resetting snd_cwnd tcp_time_stamp(tp) %u, last_ack %u, srtt %lu, snd_cwnd %u, flag %u\n",
				tcp_time_stamp(tp), ledbat->last_ack, (tp->srtt_us>>3)/(USEC_PER_SEC/TCP_TS_HZ), tp->snd_cwnd, ledbat->flag);
			#endif
		tp->snd_cwnd = min_cwnd;
		ledbat->flag &= ~LEDBAT_SLOWDOWN;
	}
	ledbat->last_ack = tcp_time_stamp(tp);

}

static struct tcp_congestion_ops tcp_ledbat = {
	.init = tcp_ledbat_init,
	.ssthresh = tcp_ledbat_ssthresh,
	.cong_avoid = tcp_ledbat_cong_avoid,
	.undo_cwnd  = tcp_reno_undo_cwnd,
	.pkts_acked = tcp_ledbat_pkts_acked,
	.release = tcp_ledbat_release,

	.owner = THIS_MODULE,
	.name = "ledbat_pp"
};

static int __init tcp_ledbat_register(void)
{
	BUILD_BUG_ON(sizeof(struct ledbat) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&tcp_ledbat);
}

static void __exit tcp_ledbat_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_ledbat);
}

module_init(tcp_ledbat_register);
module_exit(tcp_ledbat_unregister);

MODULE_AUTHOR("Avdhesh Yadav, Dodiya Parth Hiteshbhai, Agja Dhruv Shailesh Kumar");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Ledbat (Low Extra Delay Background Transport) Plus Plus");
