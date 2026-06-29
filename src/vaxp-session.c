#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#define DEFAULT_USER_FILE "/etc/vaxp/default_user"
#define CONFIG_DIR_FMT "%s/.config/vaxp"
#define CONFIG_FILE_FMT "%s/.config/vaxp/session.conf"
#define MAX_PATH 1024
#define MAX_LINE 512

void print_usage() {
    printf("Usage: vaxp-session <command> [options]\n");
    printf("Commands:\n");
    printf("  launch                 Dynamically detect user and launch their session service.\n");
    printf("  run <username>         Set up environment and execute the session for the given user.\n");
    printf("  switch [options]       Switch user or session settings.\n");
    printf("     --user <username>       Set default user to boot into.\n");
    printf("     --session <type> <cmd>  Set session for current user.\n");
    printf("     --apply                 Apply changes by restarting the launch service.\n");
}

char* trim_whitespace(char* str) {
    char *end;
    while(*str == ' ' || *str == '\n' || *str == '\r' || *str == '\t' || *str == '"') str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t' || *end == '"')) end--;
    end[1] = '\0';
    return str;
}

char* get_target_user() {
    FILE *f = fopen(DEFAULT_USER_FILE, "r");
    if (f) {
        static char buf[256];
        if (fgets(buf, sizeof(buf), f)) {
            fclose(f);
            return trim_whitespace(buf);
        }
        fclose(f);
    }
    
    // Fallback: search for first user with UID >= 1000
    struct passwd *pw;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid >= 1000 && pw->pw_uid < 60000) {
            if (strstr(pw->pw_shell, "nologin") == NULL && strstr(pw->pw_shell, "false") == NULL) {
                endpwent();
                return pw->pw_name;
            }
        }
    }
    endpwent();
    return NULL;
}

int cmd_launch() {
    char *target_user = get_target_user();
    if (!target_user || strlen(target_user) == 0) {
        fprintf(stderr, "❌ vaxp-session: No human user found!\n");
        return 1;
    }
    
    printf("✅ vaxp-session: Target user is '%s' — Preparing session...\n", target_user);
    printf("🛑 Stopping existing sessions...\n");
    system("systemctl stop 'vaxp-session@*.service' 2>/dev/null");
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "systemctl start vaxp-session@%s.service", target_user);
    printf("🚀 Starting vaxp-session@%s.service\n", target_user);
    return system(cmd);
}

int cmd_run(const char* username) {
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "❌ vaxp-session: User %s not found.\n", username);
        return 1;
    }
    
    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), CONFIG_FILE_FMT, pw->pw_dir);
    
    char session_type[64] = "wayland";
    char session_cmd[256] = "/usr/bin/aether";
    char session_desktop[256] = "aether";
    
    FILE *f = fopen(config_path, "r");
    if (f) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "SESSION_TYPE=", 13) == 0) {
                strcpy(session_type, trim_whitespace(line + 13));
            } else if (strncmp(line, "SESSION_CMD=", 12) == 0) {
                strcpy(session_cmd, trim_whitespace(line + 12));
            } else if (strncmp(line, "SESSION_DESKTOP=", 16) == 0) {
                strcpy(session_desktop, trim_whitespace(line + 16));
            }
        }
        fclose(f);
    }
    
    char xdg_runtime[256];
    snprintf(xdg_runtime, sizeof(xdg_runtime), "/run/user/%d", pw->pw_uid);
    char dbus_addr[256];
    snprintf(dbus_addr, sizeof(dbus_addr), "unix:path=/run/user/%d/bus", pw->pw_uid);
    
    setenv("XDG_SESSION_CLASS", "user", 1);
    setenv("XDG_RUNTIME_DIR", xdg_runtime, 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", dbus_addr, 1);
    setenv("XDG_SESSION_TYPE", session_type, 1);
    
    if (strcmp(session_type, "x11") == 0) {
        setenv("DISPLAY", ":0", 0);
        char xauth[MAX_PATH];
        snprintf(xauth, sizeof(xauth), "%s/.Xauthority", pw->pw_dir);
        setenv("XAUTHORITY", xauth, 0);
    }

    if (strlen(session_desktop) == 0) {
        strcpy(session_desktop, session_cmd);
    }
    setenv("XDG_CURRENT_DESKTOP", session_desktop, 1);
    
    printf("🚀 Starting %s session: %s for user %s\n", session_type, session_cmd, username);
    
    system("systemctl --user start gnome-keyring-daemon.service");
    
/* -------------------------------------------------------
 * Export environment to the existing systemd user session
 * ------------------------------------------------------- */

system(
    "systemctl --user import-environment "
    "DISPLAY "
    "XAUTHORITY "
    "WAYLAND_DISPLAY "
    "XDG_CURRENT_DESKTOP "
    "XDG_SESSION_TYPE "
    "XDG_RUNTIME_DIR"
);

system(
    "dbus-update-activation-environment --systemd "
    "DISPLAY "
    "XAUTHORITY "
    "WAYLAND_DISPLAY "
    "XDG_CURRENT_DESKTOP "
    "XDG_SESSION_TYPE "
    "XDG_RUNTIME_DIR"
);

/* -------------------------------------------------------
 * Start the Secret Service if it is not already running
 * ------------------------------------------------------- */

system("systemctl --user start gnome-keyring-daemon.service");

/* -------------------------------------------------------
 * Start the graphical session target
 * ------------------------------------------------------- */

system("systemctl --user start vaxp-session.target");

/* -------------------------------------------------------
 * Replace current process with desktop
 * ------------------------------------------------------- */

char final_cmd[512];
if (strcmp(session_type, "x11") == 0) {
    snprintf(final_cmd, sizeof(final_cmd), "startx %s", session_cmd);
} else {
    strncpy(final_cmd, session_cmd, sizeof(final_cmd));
}

char *args[] = {
    "sh",
    "-c",
    final_cmd,
    NULL
};

/* 
 * Drop any inherited ambient capabilities before launching the desktop session.
 * This prevents the Linux Kernel from marking the graphical apps as "elevated",
 * which blocks xdg-desktop-portal from accessing /proc/[PID]/root.
 */
prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);

execvp("/bin/sh", args);

perror("execvp");
return 1;
}

int cmd_switch(int argc, char *argv[]) {
    int apply = 0;
    char *new_user = NULL;
    char *new_session_type = NULL;
    char *new_session_cmd = NULL;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            new_user = argv[++i];
        } else if (strcmp(argv[i], "--session") == 0 && i + 2 < argc) {
            new_session_type = argv[++i];
            new_session_cmd = argv[++i];
        } else if (strcmp(argv[i], "--apply") == 0) {
            apply = 1;
        }
    }
    
    if (new_user) {
        if (getpwnam(new_user) == NULL) {
            fprintf(stderr, "❌ User '%s' does not exist.\n", new_user);
            return 1;
        }
        
        system("mkdir -p /etc/vaxp");
        FILE *f = fopen(DEFAULT_USER_FILE, "w");
        if (!f) {
            fprintf(stderr, "⚠️  Modifying default user requires sudo privileges.\n");
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "sudo bash -c \"mkdir -p /etc/vaxp && echo '%s' > %s\"", new_user, DEFAULT_USER_FILE);
            system(cmd);
        } else {
            fprintf(f, "%s\n", new_user);
            fclose(f);
        }
        printf("✅ Default user set to '%s'.\n", new_user);
    }
    
    if (new_session_type && new_session_cmd) {
        const char *homedir = getenv("HOME");
        if (!homedir) homedir = getpwuid(getuid())->pw_dir;
        
        char config_dir[MAX_PATH];
        snprintf(config_dir, sizeof(config_dir), CONFIG_DIR_FMT, homedir);
        
        char mkdir_cmd[MAX_PATH + 16];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", config_dir);
        system(mkdir_cmd);
        
        char config_path[MAX_PATH];
        snprintf(config_path, sizeof(config_path), CONFIG_FILE_FMT, homedir);
        
        FILE *f = fopen(config_path, "w");
        if (f) {
            char *base_cmd = strrchr(new_session_cmd, '/');
            base_cmd = base_cmd ? base_cmd + 1 : new_session_cmd;
            
            fprintf(f, "SESSION_TYPE=\"%s\"\n", new_session_type);
            fprintf(f, "SESSION_CMD=\"%s\"\n", new_session_cmd);
            fprintf(f, "SESSION_DESKTOP=\"%s\"\n", base_cmd);
            fclose(f);
            printf("✅ Session set to %s (%s) for current user.\n", new_session_type, new_session_cmd);
        } else {
            perror("fopen session.conf");
        }
    }
    
    if (apply) {
        printf("🔄 Restarting session manager...\n");
        if (geteuid() != 0) {
            system("sudo systemctl restart vaxp-launch.service");
        } else {
            system("systemctl restart vaxp-launch.service");
        }
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "launch") == 0) {
        return cmd_launch();
    } else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: vaxp-session run <username>\n");
            return 1;
        }
        return cmd_run(argv[2]);
    } else if (strcmp(argv[1], "switch") == 0) {
        return cmd_switch(argc, argv);
    } else {
        print_usage();
        return 1;
    }
}
