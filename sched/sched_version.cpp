// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#include "boinc_db.h"

#include "sched_main.h"
#include "sched_msgs.h"
#include "sched_config.h"
#include "sched_customize.h"
#include "sched_types.h"
#include "sched_util.h"
#include "credit.h"

#include "sched_version.h"

inline void dont_need_message(
    const char* p, APP_VERSION* avp, CLIENT_APP_VERSION* cavp
) {
    if (!config.debug_version_select||1) return;
    if (avp) {
        APP* app = ssp->lookup_app(avp->appid);
        log_messages.printf(MSG_NORMAL,
            "[version] Don't need %s jobs, skipping version %d for %s (%s)\n",
            p, avp->version_num, app->name, avp->plan_class
        );
    } else if (cavp) {
        log_messages.printf(MSG_NORMAL,
            "[version] Don't need %s jobs, skipping anonymous version %d for %s (%s)\n",
            p, cavp->version_num, cavp->app_name, cavp->plan_class
        );
    }
}

// for new-style requests, check that the app version uses a
// resource for which we need work
//
bool need_this_resource(
    HOST_USAGE& host_usage, APP_VERSION* avp, CLIENT_APP_VERSION* cavp
) {
    if (g_wreq->rsc_spec_request) {
        if (host_usage.ncudas) {
            if (!g_wreq->need_cuda()) {
                dont_need_message("CUDA", avp, cavp);
                return false;
            }
        } else if (host_usage.natis) {
            if (!g_wreq->need_ati()) {
                dont_need_message("ATI", avp, cavp);
                return false;
            }
        } else {
            if (!g_wreq->need_cpu()) {
                dont_need_message("CPU", avp, cavp);
                return false;;
            }
        }
    }
    return true;
}

static DB_HOST_APP_VERSION* lookup_host_app_version(int gavid) {
    for (unsigned int i=0; i<g_wreq->host_app_versions.size(); i++) {
        DB_HOST_APP_VERSION& hav = g_wreq->host_app_versions[i];
        if (hav.app_version_id == gavid) return &hav;
    }
    return NULL;
}

static inline bool app_version_is_trusted(int gavid) {
    DB_HOST_APP_VERSION* havp = lookup_host_app_version(gavid);
    if (!havp) return false;
    return havp->trusted;
}

static inline bool app_version_is_reliable(int gavid) {
    DB_HOST_APP_VERSION* havp = lookup_host_app_version(gavid);
    if (!havp) return false;
    return havp->reliable;
}

inline int host_usage_to_gavid(HOST_USAGE& hu, APP& app) {
    return app.id*1000000 - hu.resource_type();
}

inline bool daily_quota_exceeded(int gavid) {
    return false;
    DB_HOST_APP_VERSION* havp = lookup_host_app_version(gavid);
    if (!havp) return false;
    if (havp->n_jobs_today >= havp->max_jobs_per_day) {
        havp->daily_quota_exceeded = true;
        return true;
    }
    return false;
}

// scan through client's anonymous apps and pick the best one
//
CLIENT_APP_VERSION* get_app_version_anonymous(APP& app, bool reliable_only) {
    unsigned int i;
    CLIENT_APP_VERSION* best = NULL;
    bool found = false;
    char message[256];

    for (i=0; i<g_request->client_app_versions.size(); i++) {
        CLIENT_APP_VERSION& cav = g_request->client_app_versions[i];
        if (config.debug_send) {
            log_messages.printf(MSG_NORMAL,
                "[version] get_app_version_anonymous: %s %d\n",
                cav.app_name, cav.version_num
            );
        }
        if (cav.app->id != app.id) {
            continue;
        }
        int gavid = host_usage_to_gavid(cav.host_usage, app);
/*
        if (reliable_only && !app_version_is_reliable(gavid)) {
            log_messages.printf(MSG_NORMAL, "[version] not reliable\n");
            continue;
        }
*/
        if (daily_quota_exceeded(gavid)) {
            log_messages.printf(MSG_NORMAL, "[version] daily quota exceeded\n");
            continue;
        }
        if (cav.version_num < app.min_version) {
            log_messages.printf(MSG_NORMAL, "[version] bad version\n");
            continue;
        }
        found = true;
        if (!need_this_resource(cav.host_usage, NULL, &cav)) {
            log_messages.printf(MSG_NORMAL, "[version] don't need resource\n");
            continue;
        }
        if (best) {
//            if (cav.host_usage.projected_flops > best->host_usage.projected_flops) {
                best = &cav;
//            }
        } else {
            best = &cav;
        }
    }
    if (!best) {
        if (config.debug_send) {
            log_messages.printf(MSG_NORMAL,
                "[send] Didn't find anonymous platform app for %s\n",
                app.name
            );
        }
    }
    if (!found) {
        sprintf(message,
            "Your app_info.xml file doesn't have a version of %s.",
            app.user_friendly_name
        );
        add_no_work_message(message);
    }
    return best;
}

// called at start of send_work().
// Estimate FLOPS of anon platform versions,
// and compute scaling factor for wu.rsc_fpops
//
void estimate_flops_anon_platform() {
    unsigned int i;
    for (i=0; i<g_request->client_app_versions.size(); i++) {
        CLIENT_APP_VERSION& cav = g_request->client_app_versions[i];

        cav.rsc_fpops_scale = 1;

        if (cav.host_usage.avg_ncpus == 0 && cav.host_usage.ncudas == 0 && cav.host_usage.natis == 0) {
            cav.host_usage.avg_ncpus = 1;
        }

        // current clients fill in host_usage.flops with peak FLOPS
        // if it's missing from app_info.xml;
        // however, for older clients, we need to fill it in ourselves;
        // assume it uses 1 CPU
        //
        if (cav.host_usage.projected_flops == 0) {
            cav.host_usage.projected_flops = g_reply->host.p_fpops;
        }

        // At this point host_usage.projected_flops is filled in with something.
        // See if we have a better estimated based on history
        //
        DB_HOST_APP_VERSION* havp = gavid_to_havp(
            generalized_app_version_id(
                cav.host_usage.resource_type(), cav.app->id
            )
        );
        if (havp && havp->et.n > MIN_HOST_SAMPLES) {
            double new_flops = 1./havp->et.get_avg();
            cav.rsc_fpops_scale = cav.host_usage.projected_flops/new_flops;
            cav.host_usage.projected_flops = new_flops;
            if (config.debug_send) {
                log_messages.printf(MSG_NORMAL,
                    "[send] (%s) setting projected flops to %fG based on ET\n",
                    cav.plan_class, new_flops/1e9
                );
            }
        } else {
            if (config.debug_send) {
                log_messages.printf(MSG_NORMAL,
                    "[send] (%s) using client-supplied flops %fG\n",
                    cav.plan_class, cav.host_usage.projected_flops
                );
            }
        }
    }
}

// if we have enough statistics to estimate the app version's
// actual FLOPS on this host, do so.
//
void estimate_flops(HOST_USAGE& hu, APP_VERSION& av) {
    DB_HOST_APP_VERSION* havp = gavid_to_havp(av.id);
    if (havp && havp->et.n > MIN_HOST_SAMPLES) {
        double new_flops = 1./havp->et.get_avg();
        hu.projected_flops = new_flops;
        if (config.debug_send) {
            log_messages.printf(MSG_NORMAL,
                "[send] [AV#%d] (%s) setting projected flops based on host elapsed time avg: %.2fG\n",
                av.id, av.plan_class, hu.projected_flops/1e9
            );
        }
    } else {
        if (av.pfc_scale) {
            hu.projected_flops *= av.pfc_scale;
            if (config.debug_send) {
                log_messages.printf(MSG_NORMAL,
                    "[send] [AV#%d] (%s) adjusting projected flops based on PFC scale: %.2fG\n",
                    av.id, av.plan_class, hu.projected_flops/1e9
                );
            }
        } else {
            if (config.debug_send) {
                log_messages.printf(MSG_NORMAL,
                    "[send] [AV#%d] (%s) using unscaled projected flops: %.2fG\n",
                    av.id, av.plan_class, hu.projected_flops/1e9
                );
            }
        }
    }
}

// return BEST_APP_VERSION for the given host, or NULL if none
//
// check_req: check whether we still need work for the resource
// reliable_only: use only versions for which this host is "reliable"
//
BEST_APP_VERSION* get_app_version(
    WORKUNIT& wu, bool check_req, bool reliable_only
) {
    bool found;
    unsigned int i;
    int retval, j;
    BEST_APP_VERSION* bavp;
    char message[256], buf[256];

    // see if app is already in memoized array
    //
/*
    std::vector<BEST_APP_VERSION*>::iterator bavi;
    bavi = g_wreq->best_app_versions.begin();
    while (bavi != g_wreq->best_app_versions.end()) {
        bavp = *bavi;
        if (bavp->appid == wu.appid) {
            if (!bavp->present) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] returning cached NULL\n"
                    );
                }
                return NULL;
            }

            // if we previously chose a CUDA app but don't need more CUDA work,
            // delete record, fall through, and find another version
            //
            if (check_req
                && g_wreq->rsc_spec_request
                && bavp->host_usage.ncudas > 0
                && !g_wreq->need_cuda()
            ) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] have CUDA version but no more CUDA work needed\n"
                    );
                }
                g_wreq->best_app_versions.erase(bavi);
                break;
            }

            // same, ATI
            if (check_req
                && g_wreq->rsc_spec_request
                && bavp->host_usage.natis > 0
                && !g_wreq->need_ati()
            ) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] have ATI version but no more ATI work needed\n"
                    );
                }
                g_wreq->best_app_versions.erase(bavi);
                break;
            }

            // same, CPU
            //
            if (check_req
                && g_wreq->rsc_spec_request
                && !bavp->host_usage.ncudas
                && !bavp->host_usage.natis
                && !g_wreq->need_cpu()
            ) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] have CPU version but no more CPU work needed\n"
                    );
                }
                g_wreq->best_app_versions.erase(bavi);
                break;
            }

            if (config.debug_version_select||1) {
                log_messages.printf(MSG_NORMAL,
                    "[version] returning cached version\n"
                );
            }
            return bavp;
        }
        bavi++;
    }
*/

    APP* app = ssp->lookup_app(wu.appid);
    if (!app) {
        log_messages.printf(MSG_CRITICAL,
            "WU refers to nonexistent app: %d\n", wu.appid
        );
        return NULL;
    }
    if (config.debug_version_select||1) {
        log_messages.printf(MSG_NORMAL,
            "[version] looking for version of %s\n",
            app->name
        );
    }

    bavp = new BEST_APP_VERSION;
    bavp->appid = wu.appid;
    if (g_wreq->anonymous_platform) {
        CLIENT_APP_VERSION* cavp = get_app_version_anonymous(
            *app, reliable_only
        );
        if (!cavp) {
            bavp->present = false;
        } else {
            bavp->present = true;
//            if (config.debug_version_select||1) {
                log_messages.printf(MSG_NORMAL,
                    "[version] Found anonymous platform app for %s: plan class %s\n",
                    app->name, cavp->plan_class
                );
//            }
            bavp->host_usage = cavp->host_usage;
            bavp->cavp = cavp;
            int gavid = host_usage_to_gavid(cavp->host_usage, *app);
            bavp->reliable = app_version_is_reliable(gavid);
            bavp->trusted = app_version_is_trusted(gavid);
        }
        g_wreq->best_app_versions.push_back(bavp);
        if (!bavp->present) return NULL;
        return bavp;
    }

    // Go through the client's platforms.
    // Scan the app versions for each platform.
    // Find the one with highest expected FLOPS
    //
    bavp->host_usage.projected_flops = 0;
    bavp->avp = NULL;
    bool no_version_for_platform = true;
    for (i=0; i<g_request->platforms.list.size(); i++) {
        PLATFORM* p = g_request->platforms.list[i];
        for (j=0; j<ssp->napp_versions; j++) {
            HOST_USAGE host_usage;
            APP_VERSION& av = ssp->app_versions[j];
            if (av.appid != wu.appid) continue;
            if (av.platformid != p->id) continue;
            no_version_for_platform = false;

            if (daily_quota_exceeded(av.id)) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] daily quota exceeded\n"
                    );
                }
                continue;
            }
            if (reliable_only && !app_version_is_reliable(av.id)) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL, "[version] not reliable\n");

                }
                continue;
            }
            if (g_request->core_client_version < av.min_core_version) {
                log_messages.printf(MSG_NORMAL,
                    "outdated client version %d < min core version %d\n",
                    g_request->core_client_version, av.min_core_version
                );
                g_wreq->outdated_client = true;
                continue;
            }
            if (strlen(av.plan_class)) {
                if (!g_request->client_cap_plan_class) {
                    log_messages.printf(MSG_NORMAL,
                        "client version %d lacks plan class capability\n",
                        g_request->core_client_version
                    );
                    continue;
                }
                if (!app_plan(*g_request, av.plan_class, host_usage)) {
                    continue;
                }
            } else {
                host_usage.sequential_app(g_reply->host.p_fpops);
            }

            // skip versions for resources we don't need
            //
/*
            if (!need_this_resource(host_usage, &av, NULL)) {
                continue;
            }
*/

            // skip versions that go against resource prefs
            //
            if (host_usage.ncudas && g_wreq->no_cuda) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] Skipping CUDA version - user prefs say no CUDA\n"
                    );
                    g_wreq->no_cuda_prefs = true;
                }
                continue;
            }
            if (host_usage.natis && g_wreq->no_ati) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] Skipping ATI version - user prefs say no ATI\n"
                    );
                    g_wreq->no_ati_prefs = true;
                }
                continue;
            }
            if (!(host_usage.ncudas || host_usage.natis) && g_wreq->no_cpu) {
                if (config.debug_version_select||1) {
                    log_messages.printf(MSG_NORMAL,
                        "[version] Skipping CPU version - user prefs say no CPUs\n"
                    );
                    g_wreq->no_cpu_prefs = true;
                }
                continue;
            }

            estimate_flops(host_usage, av);

            // pick the fastest version
            //
            if (host_usage.projected_flops > bavp->host_usage.projected_flops) {
                bavp->host_usage = host_usage;
                bavp->avp = &av;
                bavp->reliable = app_version_is_reliable(av.id);
                bavp->trusted = app_version_is_trusted(av.id);
            }
        }
    }
    if (bavp->avp) {
        if (config.debug_version_select||1) {
            log_messages.printf(MSG_NORMAL,
                "[version] Best version of app %s is ID %d (%.2f GFLOPS)\n",
                app->name, bavp->avp->id, bavp->host_usage.projected_flops/1e9
            );
        }
        bavp->present = true;
        g_wreq->best_app_versions.push_back(bavp);
    } else {
        // Here if there's no app version we can use.
        //
        if (config.debug_version_select||1) {
            log_messages.printf(MSG_NORMAL,
                "[version] returning NULL; platforms:\n"
            );
            for (i=0; i<g_request->platforms.list.size(); i++) {
                PLATFORM* p = g_request->platforms.list[i];
                log_messages.printf(MSG_NORMAL,
                    "[version] %s\n",
                    p->name
                );
            }
        }
        if (no_version_for_platform) {
            sprintf(message,
                "%s is not available for your type of computer.",
                app->user_friendly_name
            );
            add_no_work_message(message);
        }
        g_wreq->best_app_versions.push_back(bavp);
        return NULL;
    }
    return bavp;
}

