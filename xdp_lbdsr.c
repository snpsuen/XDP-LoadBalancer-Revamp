#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>

#include "xdp_lbdsr.h"
#include "xdp_lbdsr.skel.h"

static volatile bool exitpoll = false;
static void sig_handler(int sig) {
	exitpoll = true;
}

static int32_t last_serverkey(int mfd) {
	uint32_t* current = NULL;
	int32_t last = -1;
	uint32_t next;
	struct serveraddr backend;
	while (bpf_map_get_next_key(mfd, current, &next) == 0) {
		if (bpf_map_lookup_elem(mfd, &next, &backend) == 0)
			last = next;
		
		current = &next;
	}
	
	return last;
}

static uint32_t build_serverindex(int smfd, int sifd) {
	uint32_t* current = NULL;
	uint32_t next;
	struct serveraddr backend;
	uint8_t index = 1;
	uint32_t count = 0;
	
	while (bpf_map_get_next_key(smfd, current, &next) == 0) {
		if (bpf_map_lookup_elem(smfd, &next, &backend) == 0) {
			bpf_map_update_elem(sifd, &index, &next, BPF_ANY);
			count++;
			index++;
		}
	  
		current = &next;
	}
	
	index = 0;
	bpf_map_update_elem(sifd, &index, &count, BPF_ANY);
	
	return 0;
}

uint32_t headsup_dispatch(void*, void *data, size_t)  {
    struct dispatchmsg_t* msg = (struct displatchmsg_t *)data;
    uint8_t clientip[INET_ADDRSTRLEN];
	uint8_t serverip[INET_ADDRSTRLEN];
	struct serveraddr backend;
    
    printf("--- Received and dispatched a request! ---\n");
    printf("Timestamp: %lld\n", msg->timestamp);
    inet_ntop(AF_INET, &(msg->saddr), clientip, INET_ADDRSTRLEN);
    printf(" From Client IP: %s\n", str_s);
	
	int ret = bpf_map_lookup_elem(smfd, &(msg->backendkey), &backend);
	if (ret < 0)
		fprintf(stderr, "Cannot find the server key %d (error: %s)\n", msg->backendkey, strerror(errno));
	else {
		inet_ntop(AF_INET, &(backend.ipaddr), serverip, sizeof(serverip));
		printf("To Server Key: %d ---> VIP: %s / MAC: %x:%x:%x:%x:%x:%x \n", msg->backendkey, serverip, backend.macaddr[0], backend.macaddr[1], backend.macaddr[2], backend.macaddr[3], backend.macaddr[4], backend.macaddr[5]);
	}
    
    return 0;
}

uint32_t do_backend(uint32_t smfd, uint32_t sifd) {
	int exithere = 0;
	
	while (1) {
		printf("Backend server management:\n\n")
		printf("(1) List backend server map\n\n");
		printf("(2) Add backend server\n");
		printf("(3) Update backend server\n");
		printf("(4) Delete backend server\n");
		printf("(5) Exit submenu"
		printf("Enter one of the options 1-5: ");
		
		uint32_t option;
		scanf("%d%*c", &option);
	  
		switch(option) {
		case 1:
			if (last_serverkey(mfd) < 0)
				printf("The backend server map is empty");
			else {
				uint32_t* current = NULL;
				uint32_t next;
				struct serveraddr backend;
				serverip[INET_ADDRSTRLEN];
								
				while (bpf_map_get_next_key(mfd, current, &next) == 0) {
					if (bpf_map_lookup_elem(mfd, &next, &backend) == 0) {
						inet_ntop(AF_INET, &(backend.ipaddr), serverip, sizeof(serverip));
						printf("Key: %d ---> IP: %s / MAC: %x:%x:%x:%x:%x:%x \n", next, serverip, backend.macaddr[0], backend.macaddr[1], backend.macaddr[2], backend.macaddr[3], backend.macaddr[4], backend.macaddr[5]);
					}
					else
						printf("Cannot look up key %d in the load balancer map (error: %s)\n", next, strerror(errno));
					
					current = &next;
				}
			}
			
			printf("Press enter to return to the submenu ... ");
			getc(stdin);
			
			break;
		
		case 2:
			while (1) {
				uint8_t serverip[INET_ADDRSTRLEN];
				int32_t macint[6];
				uint8_t machar[6];
				struct serveraddr backend;
				
				memset(serverip, 0, sizeof(serverip));
				printf("Enter the server IP in the form of xxx.xxx.xxx.xxx ---> ");
				fgets(serverip, sizeof(serverip), stdin);
				serverip[strlen(serverip)-1] = 0;
				inet_pton(AF_INET, serverip, &(backend.ipaddr));
				
				printf("Enter an MAC address in the form xx:xx:xx:xx:xx:xx --> ");
				scanf("%x:%x:%x:%x:%x:%x%*c", &macint[0], &macint[1], &macint[2], &macint[3], &macint[4], &macint[5])
				for (int i = 0; i < 6; i++)
					backend.macaddr[i] = (uint8_t)macint[i];
			
				uint32_t addrkey = last_serverkey(smfd) + 1;
				int ret = bpf_map_update_elem(smfd, &addrkey, &backend, BPF_ANY);
				if (ret < 0)
					fprintf(stderr, "Cannot add an backend server at key %d (error: %s)\n", addrkey, strerror(errno));
				else {
					fprintf(stderr, "Added a backend server at key %d (error: %s)\n", addrkey, strerror(errno));
					build_serverindex(smfd, sifd);
				}
				
				uint8_t ans[8];
				memset(ans, 0, sizeof(ans));
				printf("Any more backend server to add? (Y/N): ");
				fgets(ans, sizeof(ans), stdin);
				ans[strlen(ans)-1] = 0;
				
				if ((strcmp(ans, "Y") != 0) && (strcmp(ans, "y") != 0))
					break;
				
			}
			
			break;
			
		case 3:
			while (1) {
				uint32_t addrkey;
				struct serveraddr backend;
				
				printf("Enter the server key to update: ");
				scanf("%d%*c", &addrkey);
				
				int ret = bpf_map_lookup_elem(smfd, &addrkey, &backend);
				if (ret < 0)
					fprintf(stderr, "Cannot find the server key %d (error: %s)\n", addrkey, strerror(errno));
				else {
					uint8_t serverip[INET_ADDRSTRLEN];
					uint32_t macint[6];
					
					memset(serverip, 0, sizeof(serverip));
					printf("Enter the IP of the server to be updated in the form of xxx.xxx.xxx.xxx ---> ");
					fgets(serverip, sizeof(serverip), stdin);
					serverip[strlen(serverip)-1] = 0;
					inet_pton(AF_INET, serverip, &(backend.ipaddr));
					
					printf("Enter the MAC address for the server to be updated in the form xx:xx:xx:xx:xx:xx --> ");
					scanf("%x:%x:%x:%x:%x:%x%*c", &macint[0], &macint[1], &macint[2], &macint[3], &macint[4], &macint[5]);
					for (int i = 0; i < 6; i++)
						backend.macaddr[i] = (uint8_t)macint[i];
					
					ret = bpf_map_update_elem(smfd, &addrkey, &backend, BPF_ANY);
					if (ret < 0)
						fprintf(stderr, "Cannot update the backend server at key %d (error: %s)\n", addrkey, strerror(errno));
					else {
						fprintf(stderr, "Updated the backend server at key %d\n", addrkey);
						build_serverindex(smfd, sifd);
					}
				}
				
				uint8_t ans[8];
				memset(ans, 0, sizeof(ans));
				printf("Any more backend server to update? (Y/N): ");
				fgets(ans, sizeof(ans), stdin);
				ans[strlen(ans)-1] = 0;
				
				if ((strcmp(ans, "Y") != 0) && (strcmp(ans, "y") != 0))
					break;
				
			}
			
			break;
		
		case 4:
			while (1) {
				uint32_t addrkey;
				struct serveraddr backend;
				
				printf("Enter the server key to delete: ");
				scanf("%d%*c", &addrkey);
				
				int ret = bpf_map_lookup_elem(smfd, &addrkey, &backend);
				if (ret < 0)
					fprintf(stderr, "Cannot find the server key %d (error: %s)\n", addrkey, strerror(errno));
				else {
					ret = bpf_map_delete_elem(smfd);
					if (ret < 0) 
						fprintf(stderr, "Cannot delete the server key %d (error: %s)\n", addrkey, strerror(errno));
					else {
						fprintf(stderr, "Deleted the backend server at key %d\n", addrkey);
						build_serverindex(smfd, sifd);
					}						
				}
				
				uint8_t ans[8];
				memset(ans, 0, sizeof(ans));
				printf("Any more backend server to delete? (Y/N): ");
				fgets(ans, sizeof(ans), stdin);
				ans[strlen(ans)-1] = 0;
				
				if ((strcmp(ans, "Y") != 0) && (strcmp(ans, "y") != 0))
					break;
				
			}
			
			break;
				
		case 5:
			exithere = 1;
			break;
			
		case default:
			break;
		
		}
		
		if (exithere == 1)
			break;
		
	}
	
	return 0;
}

uint32_t do_loadbalancer(uint32_t lmfd) {
	uint32_t* current = NULL;
	uint32_t next;
	struct serveraddr backend;
	uint8_t serverip[INET_ADDRSTRLEN];
	
	if (bpf_map_get_next_key(lmfd, current, &next) == 0) {
		if (bpf_map_lookup_elem(mfd, &next, &backend) == 0) {
			inet_ntop(AF_INET, &(backend.ipaddr), serverip, sizeof(serverip));
			printf("Key: %d ---> VIP: %s / MAC: %x:%x:%x:%x:%x:%x \n", next, serverip, backend.macaddr[0], backend.macaddr[1], backend.macaddr[2], backend.macaddr[3], backend.macaddr[4], backend.macaddr[5]);
		}
		else
			printf("Cannot look up key %d in the load balancer map (error: %s)\n", next, strerror(errno));
	}
	else if (errno == ENOENT)
		printf("The load balancer map is empty");
	else
		printf("Cannot get the next key from the load balancer map (error: %s)\n", next, strerror(errno));
	
	uint8_t ans[8];
	memset(ans, 0, sizeof(ans));
	printf("Do you want to continue? (Y/N):  ");
	fgets(ans, sizeof(ans), stdin);
	ans[strlen(ans)-1] = 0;
	
	if ((strcmp(ans, "Y") = 0) || (strcmp(ans, "y") == 0)) {
		uint32_t addrkey = 0;
		struct serveraddr loadbalancer;
		uint8_t serverip[INET_ADDRSTRLEN];
		uint32_t macint[6];
					
		memset(serverip, 0, sizeof(serverip));
		printf("Enter the VIP controlled by the load balancer in the form of xxx.xxx.xxx.xxx ---> ");
		fgets(serverip, sizeof(serverip), stdin);
		serverip[strlen(serverip)-1] = 0;
		inet_pton(AF_INET, serverip, &(loadbalancer.ipaddr));
					
		printf("Enter the MAC address of the load balancer in the form xx:xx:xx:xx:xx:xx --> ");
		scanf("%x:%x:%x:%x:%x:%x%*c", &macint[0], &macint[1], &macint[2], &macint[3], &macint[4], &macint[5]);
		for (int i = 0; i < 6; i++)
			loadbalancer.macaddr[i] = (uint8_t)macint[i];
					
		int ret = bpf_map_update_elem(lmfd, &addrkey, &loadbalancer, BPF_ANY);
		if (ret < 0)
			fprintf(stderr, "Cannot update the load balancer server (error: %s)\n", strerror(errno));
		else {
			addrkey = 0;
			memset(&loadbalancer, 0, sizeof(loadbalancer));
			memset(serverip, 0, sizeof(serverip));

			if (bpf_map_lookup_elem(lmfd, &addrkey, &loadbalancer) == 0) {
				inet_ntop(AF_INET, &(loadbalancer.ipaddr), serverip, sizeof(serverip));
				printf("Load balancer update confirmed\n")
				printf("Key: %d ---> VIP: %s / MAC: %x:%x:%x:%x:%x:%x \n", addrkey, serverip, loadbalancer.macaddr[0], loadbalancer[1], loadbalancer[2], loadbalancer.macaddr[3], loadbalancer.macaddr[4], loadbalancer.macaddr[5]);
			}
			else
				fprintf(stderr, "Cannot confirm update entry (error: %s)\n", strerror(errno));
		}
	}
	
	return 0;
}
		
int32_t do_dispatch(struct ring_buffer* ringbuf, uint32_t interval) {
	int32_t err;
	
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	
	while (!exitpoll) {
		printf("Press Control-C to exit the poll loop ...\n")
		err = ring_buffer__poll(ringbuf, interval /* timeout, ms */);
		/* Ctrl-C will cause -EINTR */
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling ring buffer: %d\n", err);
			break;
		}
	}
	
	exitpoll = false;
	return err;
}


uint32_t do_exit(void) {
	char ans[MAXRES];
	
	memset(ans, 0, sizeof(ans));
	printf("Please confirm you want to exit the control plane (Y/n): ")
    if (fgets(ans, sizeof(ans), stdin) == NULL) { 
		printf("Fail to read the input stream"); 
		return 0;
    }
	
    ans[strlen(ans)-1] = 0;
    if ((strcmp(ans, "Y") == 0) || (strcmp(ans, "y") == 0))
		return 1;
	
	return 0;
}

int main(int argc, char *argv[]) {
    uint32_t ifindex;
    uint8_t* ifname;
    uint32_t interval;
	uint32_t exitcon = 0;
    
    switch(argc) {
        case 1:
            ifname = "eth0";
            interval = 1000;
            break;
        case 2:
            ifname = argv[1];
            interval = 1000;
            break;
        default:
            ifname = argv[1];
            interval = atoi(argv[2]);
    }

    ifindex = if_nametoindex(ifname);
    if (!ifindex) {
        perror("failed to resolve iface to ifindex");
        return EXIT_FAILURE;
    }

    struct rlimit rlim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };
    if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
        perror("failed to increase RLIMIT_MEMLOCK");
        return EXIT_FAILURE;
    }
	    
    // Load and verify BPF application
    struct xdp_lbdsr_bpf* lbdbpf = xdp_lbdsr_bpf__open_and_load();
    if (!dpbpf) {
        fprintf(stderr, "Failed to open and open BPF object\n");
        return EXIT_FAILURE;
    }

    // Attach xdp program to interface
    struct bpf_link* lbdlink = bpf_program__attach_xdp(lbdbpf->progs.processping, ifindex);
    if (!lbdlink) {
        fprintf(stderr, "Failed to call bpf_program__attach_xdp\n");
        return EXIT_FAILURE;
    }
	
	int smfd = bpf_object__find_map_fd_by_name(lbdbpf->obj, "server_map");
    if (smfd < 0) {
        fprintf(stderr, "Failed to find the fd for the backend server map (error: %s))\n", strerror(errno));
        return EXIT_FAILURE;
    }
	
	int sifd = bpf_object__find_map_fd_by_name(lbdbpf->obj, "serverindex_map");
    if (sifd < 0) {
        fprintf(stderr, "Failed to find the fd for the server index map (error: %s))\n", strerror(errno));
        return EXIT_FAILURE;
    }
	
	int lmfd = bpf_object__find_map_fd_by_name(lbdbpf->obj, "lb_map");
    if (lmfd < 0) {
        fprintf(stderr, "Failed to find the fd for the load balancer map (error: %s))\n", strerror(errno));
        return EXIT_FAILURE;
    }

	int drfd = bpf_object__find_map_fd_by_name(lbdbpf->obj, "dispatch_ring");
    if (drfd < 0) {
        fprintf(stderr, "Failed to find the fd for the dispatch ring map (error: %s))\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct ring_buffer* ringbuf = ring_buffer__new(drfd, headsup_dispatch, NULL, NULL);
    if (!ringbuf) {
        fprintf(stderr, "Failed to create ring buffer\n");
        return EXIT_FAILURE;
    }

	while (1) {
		printf("Load balancer control plane\n\n");
		printf("(1) Backend server management\n");
		printf("(2) Load balancer management\n");
		printf("(3) Dispatched workload tracking\n");
		printf("(4) Exit control plane\n\n");
		printf("Enter one of the options 1-4: ");
	
		int option;
		scanf("%d%*c", &option);
	  
		switch(option) {
		case 1:
			do_backend(smfd, sifd);
            break;
        case 2:
            do_loadbalancer(lmfd);
            break;
		case 3:
            do_disptach(ringbuf, interval);
            break;
		case 4:
			exitcon = do_exit();
            break;
        default:
			break;
		}
		
		if (exitcon == 1)
			break;
		
    }
	
	return 0
}
