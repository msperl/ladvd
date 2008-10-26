/*
 $Id$
*/

#include "common.h"
#include "main.h"
#include "util.h"
#include <ctype.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#ifdef USE_CAPABILITIES
#include <sys/prctl.h>
#include <sys/capability.h>
#endif

extern unsigned int loglevel;
unsigned int do_fork = 1;
unsigned int do_debug = 0;

void usage(const char *fn);

int main(int argc, char *argv[]) {

    int ch, p, run_once = 0;
    char *progname = argv[0];
    char *username = PACKAGE_USER;
#ifndef __APPLE__
    char *pidfile = PACKAGE_PID_FILE;
    char pidstr[16];
    int fd = -1;
#endif /* __APPLE__ */
    struct passwd *pwd = NULL;

    // sysinfo
    struct sysinfo sysinfo;

    // socket
    int sockfd;

    // packet
    char packet [ETHER_MAX_LEN];
    size_t len;

    // interfaces
    struct netif *netifs = NULL, *netif, *master;

    // pcap
    pcap_hdr_t pcap_hdr;

#ifdef USE_CAPABILITIES
    // capabilities
    cap_t caps;
#endif

    /* set arguments */
    memset(&sysinfo, 0, sizeof(struct sysinfo));

    while ((ch = getopt(argc, argv, "dfhlm:nou:vc:l:CEFN")) != -1) {
	switch(ch) {
	    case 'd':
		do_debug = 1;
		do_fork = 0;
		break;
	    case 'f':
		do_fork = 0;
		break;
	    case 'm':
		if ( (inet_pton(AF_INET, optarg, &sysinfo.maddr4) != 1) &&
		     (inet_pton(AF_INET6, optarg, &sysinfo.maddr6) != 1) ) {
		    my_log(CRIT, "invalid management address %s", optarg);
		    usage(progname);
		}
		break;
	    case 'n':
		sysinfo.maddr_force = 1;
		break;
	    case 'o':
		run_once = 1;
		break;
	    case 'u':
		username = optarg;
		break;
	    case 'v':
		loglevel++;
		break;
	    case 'c':
		// two-letter ISO 3166 country code
		if (strlen(optarg) != 2)
		    usage(progname);
		// in capital ASCII letters
		sysinfo.country[0] = toupper(optarg[0]);
		sysinfo.country[1] = toupper(optarg[1]);
		break;
	    case 'l':
		if (strlcpy(sysinfo.location, optarg, 
			sizeof(sysinfo.location)) == 0)
		    usage(progname);
		break;
	    case 'C':
		protos[PROTO_CDP].enabled = 1;
		break;
	    case 'E':
		protos[PROTO_EDP].enabled = 1;
		break;
	    case 'F':
		protos[PROTO_FDP].enabled = 1;
		break;
	    case 'N':
		protos[PROTO_NDP].enabled = 1;
		break;
	    default:
		usage(progname);
	}
    }

    argc -= optind;
    argv += optind;

    // validate interfaces
    if (netif_fetch(argc, argv, &sysinfo, &netifs) == 0) {
	my_log(CRIT, "unable fetch interfaces");
	exit(EXIT_FAILURE);
    }

    // validate username
    if ((do_debug == 0) && (pwd = getpwnam(username)) == NULL) {
	my_log(CRIT, "User %s does not exist", username);
	exit(EXIT_FAILURE);
    }

    // fetch system details
    sysinfo_fetch(&sysinfo);

#ifndef __APPLE__
    // open pidfile
    if (do_fork == 1) {
	fd = open(pidfile, O_WRONLY|O_CREAT, 0666);
	if (fd == -1) {
	    my_log(CRIT, "failed to open pidfile %s: %s",
			pidfile, strerror(errno));
	    exit(EXIT_FAILURE);	
	}
	if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
	    my_log(CRIT, PACKAGE_NAME " already running (%s locked)", pidfile);
	    exit(EXIT_FAILURE);	
	}
    }
#endif /* __APPLE__ */

    // open a raw socket
    sockfd = my_rsocket();

    if (sockfd < 0) {
	my_log(CRIT, "opening raw socket failed");
	exit(EXIT_FAILURE);
    }

    // debug
    if (do_debug != 0) {

	// zero
	memset(&pcap_hdr, 0, sizeof(pcap_hdr));

	// create pcap global header
	pcap_hdr.magic_number = PCAP_MAGIC;
	pcap_hdr.version_major = 2;
	pcap_hdr.version_minor = 4;
	pcap_hdr.snaplen = ETHER_MAX_LEN;
	pcap_hdr.network = 1;

	// send pcap global header
	(void) my_rsend(sockfd, NULL, &pcap_hdr, sizeof(pcap_hdr));

	goto loop;
    }

#ifndef __APPLE__
    // fork
    if (do_fork == 1) {
	if (daemon(0,0) == -1) {
	    my_log(CRIT, "backgrounding failed: %s", strerror(errno));
	    exit(EXIT_FAILURE);
	}

	if ((snprintf(pidstr, sizeof(pidstr), "%d\n", (int)getpid()) <= 0) ||
	    (write(fd, pidstr, strlen(pidstr)) <= 0)) {
	    my_log(CRIT, "failed to write pidfile: %s", strerror(errno));
	    exit(EXIT_FAILURE);
	}
    }
#endif /* __APPLE__ */

#ifdef USE_CAPABILITIES
    // keep capabilities
    if (prctl(PR_SET_KEEPCAPS,1) == -1) {
	my_log(CRIT, "unable to keep capabilities: %s", strerror(errno));
       	exit(EXIT_FAILURE);
    }
#endif

    // setuid & setgid
    if (setgid(pwd->pw_gid) == -1){
	my_log(CRIT, "unable to setgid: %s", strerror(errno));
       	exit(EXIT_FAILURE);
    }

    if (setgroups(0, NULL) == -1){
	my_log(CRIT, "unable to setgroups: %s", strerror(errno));
       	exit(EXIT_FAILURE);
    }

    if (setuid(pwd->pw_uid) == -1){
   	my_log(CRIT, "unable to setuid: %s", strerror(errno));
       	exit(EXIT_FAILURE);
    }


#ifdef USE_CAPABILITIES
    // keep CAP_NET_ADMIN
    caps = cap_from_text("cap_net_admin=ep");

    if (caps == NULL) {
	my_log(CRIT, "unable to create capabilities: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }

    if (cap_set_proc(caps) == -1) {
	my_log(CRIT, "unable to set capabilities: %s", strerror(errno));
	exit(EXIT_FAILURE);
    }

    (void) cap_free(caps);
#endif


loop: 
    // startup message
    my_log(CRIT, PACKAGE_STRING " running");

    while (sockfd) {

	// create netifs
	my_log(INFO, "fetching all interfaces"); 
	if (netif_fetch(argc, argv, &sysinfo, &netifs) == 0) {
	    my_log(CRIT, "unable fetch interfaces");
	    goto sleep;
	}

	for (netif = netifs; netif != NULL; netif = netif->next) {
	    // skip autodetected slaves
	    if ((argc == 0) && (netif->slave == 1))
		continue;

	    // skip unlisted interfaces
	    if ((argc > 0) && (netif->argv == 0))
		continue;

	    // skip masters without slaves
	    if ((netif->type > 0) && (netif->subif == NULL)) {
		my_log(INFO, "skipping interface %s", netif->name); 
		continue;
	    }

	    my_log(INFO, "starting loop with interface %s", netif->name); 

	    // point netif to subif when netif is master
	    master = netif;

	    if (master->type > 0)
		netif = master->subif;

	    while (master != NULL) {
		// fetch interface media status
		my_log(INFO, "fetching %s media details", netif->name);
		if (netif_media(netif) == EXIT_FAILURE) {
		    my_log(CRIT, "error fetching interface media details");
		}

		// generate and send packets
		for (p = 0; protos[p].name != NULL; p++) {

		    // only enabled protos
		    if (protos[p].enabled == 0)
			continue;

		    // clear packet
		    memset(&packet, 0, ETHER_MAX_LEN);

		    my_log(INFO, "building %s packet for %s", 
				  protos[p].name, netif->name);
		    len = protos[p].build_packet(&packet, netif, &sysinfo);
		    if (len == 0) {
			my_log(CRIT, "can't generate %s packet for %s",
				  protos[p].name, netif->name);
			continue;
		    }

		    // write it to the wire.
		    my_log(INFO, "sending %s packet (%d bytes) on %s",
				  protos[p].name, len, netif->name);
		    if (my_rsend(sockfd, netif, &packet, len) != len) {
			my_log(CRIT, "network transmit error on %s",
				  netif->name);
		    }
		}

		// point netif to the next subif
		if (master->type == 0) {
		    master = NULL;
		} else if (netif->subif != NULL) {
		    netif = netif->subif;
		} else {
		    netif = master;
		    master = NULL;
		}
	    }
	}

sleep:
	if (run_once == 1)
	    return (EXIT_SUCCESS);

	my_log(INFO, "sleeping for %d seconds", SLEEPTIME);
	(void) sleep(SLEEPTIME);
    }

    return (EXIT_SUCCESS);
}

void usage(const char *fn) {

    fprintf(stderr, "%s version %s\n" 
	"Usage: %s [-c] [-l] [-f] INTERFACE INTERFACE\n"
	    "\t-d = Dump pcap-compatible packets to stdout\n"
	    "\t-f = Run in the foreground\n"
	    "\t-h = Print this message\n"
	    "\t-m <address> = Management address (IPv4 and IPv6 supported)\n"
	    "\t-n = Use addresses specified via -m for all interfaces\n"
	    "\t-o = Run Once\n"
	    "\t-u <user> = Setuid User (defaults to %s)\n"
	    "\t-v = Increase logging verbosity\n"
	    "\t-c <CC> = System Country Code\n"
	    "\t-l <location> = System Location\n"
	    "\t-C = Enable CDP\n"
	    "\t-E = Enable EDP\n"
	    "\t-F = Enable FDP\n"
	    "\t-N = Enable NDP\n",
	    PACKAGE_NAME, PACKAGE_VERSION, fn, PACKAGE_USER);

    exit(EXIT_FAILURE);
}

