#!/QOpenSys/usr/bin/sh
#
# setup_test_groups_ibmi.sh
#
# IBM i PASE Shell Script to Create/Remove Test Groups and Users
# Uses IBM i CL commands via the 'system' utility
#
# Prerequisites:
#   - Must run as a user with *SECADM (Security Administrator) authority
#   - Typically QSECOFR or a profile with *ALLOBJ and *SECADM
#
# Usage:
#   ./setup_test_groups_ibmi.sh setup [N]        # Create group with N members
#   ./setup_test_groups_ibmi.sh addusers N       # Add N more users to group
#   ./setup_test_groups_ibmi.sh adduser <name>   # Add specific user to group
#   ./setup_test_groups_ibmi.sh rmuser <name> [-d]  # Remove user from group
#   ./setup_test_groups_ibmi.sh setmembers N     # Set group to exactly N members
#   ./setup_test_groups_ibmi.sh cleanup          # Remove all test profiles
#   ./setup_test_groups_ibmi.sh status           # Show current status
#   ./setup_test_groups_ibmi.sh help             # Show help
#
# Note: The 'system' command is provided by IBM i PASE to run CL commands
#       SQL queries use QShell db2 utility (RUNSQL does not support SELECT)
#

#----------------------------------------------------------------------
# Configuration
#----------------------------------------------------------------------

PREFIX="ZTEST"
GROUP_NAME="ZTEST_GRP"
USER_PREFIX="ZTESTU"
BASE_GID=59900
BASE_UID=59900
DEFAULT_MEMBERS=50

# Home directory base (in IFS)
# Note: /home often doesn't exist on IBM i; /QOpenSys/home is standard
HOME_BASE="/QOpenSys/home"

#----------------------------------------------------------------------
# Logging functions (always verbose)
#----------------------------------------------------------------------

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_warn() {
    echo "[WARN] $1"
}

log_create() {
    echo "[CREATE] $1"
}

log_skip() {
    echo "[SKIP] $1"
}

log_delete() {
    echo "[DELETE] $1"
}

log_add() {
    echo "[ADD] $1"
}

log_remove() {
    echo "[REMOVE] $1"
}

#----------------------------------------------------------------------
# CL Command helpers
#----------------------------------------------------------------------

# Run a CL command and check result
# Usage: run_cl "COMMAND" ["yes" to ignore errors]
run_cl() {
    _cmd="$1"
    _ignore_error="${2:-no}"

    # Use system command to run CL
    # -q = quiet (suppress message output to stdout/stderr)
    _output=$(system -q "$_cmd" 2>&1)
    _rc=$?

    if [ $_rc -ne 0 ] && [ "$_ignore_error" != "yes" ]; then
        log_warn "CL command failed (rc=$_rc): $_cmd"
        if [ -n "$_output" ]; then
            log_warn "Output: $_output"
        fi
    fi

    return $_rc
}

# Check if user profile exists
# Uses CHKOBJ - no QSYS/ prefix needed for user profiles
profile_exists() {
    _profile="$1"
    system -q "CHKOBJ OBJ($_profile) OBJTYPE(*USRPRF)" 2>/dev/null
    return $?
}

#----------------------------------------------------------------------
# SQL Query helpers
# Note: RUNSQL does NOT support SELECT statements!
# Must use QShell db2 utility for queries
#----------------------------------------------------------------------

# Run SQL SELECT query using QShell db2 utility
# Usage: run_sql_query "SELECT ..."
# Returns: query output on stdout
run_sql_query() {
    _sql="$1"
    /QOpenSys/usr/bin/qsh -c "db2 \"$_sql\"" 2>/dev/null
}

# Get count of members in the group using SQL
get_member_count() {
    _grp="$1"

    # Use QShell db2 for SELECT query
    _result=$(run_sql_query "SELECT COUNT(*) FROM QSYS2.GROUP_PROFILE_ENTRIES WHERE GROUP_PROFILE_NAME = '$_grp'")

    # Parse the numeric result (skip header lines, get the number)
    _count=$(echo "$_result" | grep -E '^\s*[0-9]+\s*$' | head -1 | tr -d ' ')

    if [ -z "$_count" ]; then
        echo "0"
    else
        echo "$_count"
    fi
}

# Get the highest numbered test user that exists
get_highest_user_number() {
    _highest=0
    _i=1

    # Quick scan first 100
    while [ $_i -le 100 ]; do
        _uname=$(printf "%s%03d" "$USER_PREFIX" $_i)
        if profile_exists "$_uname"; then
            _highest=$_i
        fi
        _i=$((_i + 1))
    done

    # If found 100, continue scanning
    if [ $_highest -eq 100 ]; then
        while [ $_i -le 999 ]; do
            _uname=$(printf "%s%03d" "$USER_PREFIX" $_i)
            if profile_exists "$_uname"; then
                _highest=$_i
            else
                break
            fi
            _i=$((_i + 1))
        done
    fi

    echo "$_highest"
}

# Check if user is a member of the group
is_member() {
    _user="$1"
    _grp="$2"

    # Use QShell db2 for SELECT query
    _result=$(run_sql_query "SELECT COUNT(*) FROM QSYS2.GROUP_PROFILE_ENTRIES WHERE GROUP_PROFILE_NAME = '$_grp' AND USER_PROFILE_NAME = '$_user'")

    # Parse the numeric result
    _count=$(echo "$_result" | grep -E '^\s*[0-9]+\s*$' | head -1 | tr -d ' ')

    # Check if count >= 1 (defensive: handle any positive number)
    if [ -n "$_count" ] && [ "$_count" -ge 1 ] 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

#----------------------------------------------------------------------
# Check authority
#----------------------------------------------------------------------

check_authority() {
    log_info "Checking authority..."

    # Get current user from PASE environment
    # Note: RTVJOBA CURUSER(?N) doesn't work from PASE (CL variable syntax)
    _curuser=${USER:-$(whoami)}
    _curuser=$(echo "$_curuser" | tr '[:lower:]' '[:upper:]')

    log_info "Current user: $_curuser"
    log_info "Note: Requires *SECADM authority to create/delete user profiles"
    echo ""
}

#----------------------------------------------------------------------
# Group profile functions
#----------------------------------------------------------------------

# Create group profile
create_group() {
    _grpname="$1"
    _gid="$2"

    if profile_exists "$_grpname"; then
        log_skip "Group profile $_grpname already exists"
        return 0
    fi

    # CRTUSRPRF with GID makes it a group profile
    # GRPPRF(*NONE) is required for group profiles
    # STATUS(*DISABLED) prevents sign-on (security)
    # INLMNU(*SIGNOFF) signs off immediately if accessed
    run_cl "CRTUSRPRF USRPRF($_grpname) PASSWORD(*NONE) USRCLS(*USER) GID($_gid) GRPPRF(*NONE) STATUS(*DISABLED) INLMNU(*SIGNOFF) TEXT('Test group for getgrent')"

    if [ $? -eq 0 ]; then
        log_create "Group profile $_grpname (GID=$_gid)"
        return 0
    else
        log_error "Failed to create group profile $_grpname"
        return 1
    fi
}

# Delete group profile
delete_group() {
    _grpname="$1"

    if ! profile_exists "$_grpname"; then
        log_skip "Group profile $_grpname does not exist"
        return 0
    fi

    # OWNOBJOPT(*DLT) deletes owned objects
    # For test profiles this is appropriate
    run_cl "DLTUSRPRF USRPRF($_grpname) OWNOBJOPT(*DLT)" "yes"

    if [ $? -eq 0 ]; then
        log_delete "Group profile $_grpname"
        return 0
    else
        log_error "Failed to delete group profile $_grpname"
        return 1
    fi
}

#----------------------------------------------------------------------
# User profile functions
#----------------------------------------------------------------------

# Create user profile with group membership
create_user() {
    _username="$1"
    _uid="$2"
    _grpname="$3"

    if profile_exists "$_username"; then
        log_skip "User profile $_username already exists"
        return 0
    fi

    # Create user with specified UID and group membership
    # STATUS(*DISABLED) prevents sign-on (security for test users)
    # INLMNU(*SIGNOFF) signs off immediately if accessed
    # Note: HOMEDIR sets the path but does NOT create the directory
    run_cl "CRTUSRPRF USRPRF($_username) PASSWORD(*NONE) USRCLS(*USER) UID($_uid) GRPPRF($_grpname) STATUS(*DISABLED) INLMNU(*SIGNOFF) HOMEDIR('$HOME_BASE/$_username') TEXT('Test user for getgrent')" "yes"

    if [ $? -eq 0 ]; then
        log_create "User profile $_username (UID=$_uid, group=$_grpname)"
        # Optionally create home directory (usually not needed for test users)
        # mkdir -p "$HOME_BASE/$_username" 2>/dev/null
        return 0
    else
        log_error "Failed to create user profile $_username"
        return 1
    fi
}

# Delete user profile
delete_user() {
    _username="$1"

    if ! profile_exists "$_username"; then
        log_skip "User profile $_username does not exist"
        return 0
    fi

    run_cl "DLTUSRPRF USRPRF($_username) OWNOBJOPT(*DLT)" "yes"

    if [ $? -eq 0 ]; then
        log_delete "User profile $_username"
        return 0
    else
        log_error "Failed to delete user profile $_username"
        return 1
    fi
}

# Add user to group (change GRPPRF)
# Note: This sets PRIMARY group; user can only have ONE primary group
add_user_to_group() {
    _username="$1"
    _grpname="$2"

    if ! profile_exists "$_username"; then
        log_error "User profile $_username does not exist"
        return 1
    fi

    # Change primary group of user
    run_cl "CHGUSRPRF USRPRF($_username) GRPPRF($_grpname)"

    if [ $? -eq 0 ]; then
        log_add "User $_username to group $_grpname"
        return 0
    else
        log_error "Failed to add user $_username to group $_grpname"
        return 1
    fi
}

# Remove user from group (set GRPPRF to *NONE)
remove_user_from_group() {
    _username="$1"

    if ! profile_exists "$_username"; then
        log_skip "User profile $_username does not exist"
        return 0
    fi

    # Change primary group to *NONE
    run_cl "CHGUSRPRF USRPRF($_username) GRPPRF(*NONE)"

    if [ $? -eq 0 ]; then
        log_remove "User $_username from group"
        return 0
    else
        log_error "Failed to remove user $_username from group"
        return 1
    fi
}

# Create multiple users in a range
create_users_range() {
    _start=$1
    _end=$2
    _grpname=$3
    _created=0
    _skipped=0

    log_info "Creating user profiles ${USER_PREFIX}$(printf '%03d' $_start) to ${USER_PREFIX}$(printf '%03d' $_end)..."

    _i=$_start
    while [ $_i -le $_end ]; do
        _uname=$(printf "%s%03d" "$USER_PREFIX" $_i)
        _uid=$((BASE_UID + _i))

        if profile_exists "$_uname"; then
            log_skip "User profile $_uname already exists"
            _skipped=$((_skipped + 1))
        else
            run_cl "CRTUSRPRF USRPRF($_uname) PASSWORD(*NONE) USRCLS(*USER) UID($_uid) GRPPRF($_grpname) STATUS(*DISABLED) INLMNU(*SIGNOFF) HOMEDIR('$HOME_BASE/$_uname') TEXT('Test user for getgrent')" "yes"
            if [ $? -eq 0 ]; then
                log_create "User profile $_uname (UID=$_uid)"
                _created=$((_created + 1))
            else
                log_error "Failed to create user profile $_uname"
            fi
        fi

        _i=$((_i + 1))
    done

    log_info "Users: $_created created, $_skipped skipped (already existed)"
}

#----------------------------------------------------------------------
# Command: setup [N] - Create group with N members
#----------------------------------------------------------------------

do_setup() {
    _num_members=${1:-$DEFAULT_MEMBERS}

    echo "=============================================="
    echo "Setup: Create group with $_num_members members"
    echo "=============================================="
    echo ""
    log_info "Group profile: $GROUP_NAME (GID=$BASE_GID)"
    log_info "User profiles: ${USER_PREFIX}001 to ${USER_PREFIX}$(printf '%03d' $_num_members)"
    echo ""

    check_authority

    # Create group profile
    log_info "Creating group profile..."
    create_group "$GROUP_NAME" "$BASE_GID"
    if [ $? -ne 0 ]; then
        log_error "Failed to create group profile. Check authority."
        return 1
    fi
    echo ""

    # Create user profiles
    create_users_range 1 $_num_members "$GROUP_NAME"

    echo ""
    log_info "Setup complete!"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: addusers N - Add N more users to group
#----------------------------------------------------------------------

do_addusers() {
    _add_count=$1

    if [ -z "$_add_count" ] || [ "$_add_count" -le 0 ] 2>/dev/null; then
        log_error "Usage: $0 addusers <count>"
        log_error "  count must be a positive number"
        exit 1
    fi

    echo "=============================================="
    echo "Add $_add_count more users to group"
    echo "=============================================="
    echo ""

    # Check group exists
    if ! profile_exists "$GROUP_NAME"; then
        log_error "Group profile $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    check_authority

    # Get current state
    _current_count=$(get_member_count "$GROUP_NAME")
    _highest_user=$(get_highest_user_number)

    log_info "Current group members: $_current_count"
    log_info "Highest existing user number: $_highest_user"
    echo ""

    # Calculate range for new users
    _start=$((_highest_user + 1))
    _end=$((_highest_user + _add_count))

    log_info "Will create users $_start to $_end"
    echo ""

    # Create new users
    create_users_range $_start $_end "$GROUP_NAME"

    echo ""
    log_info "Added $_add_count users to group"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: adduser <name> - Add specific user to group
#----------------------------------------------------------------------

do_adduser() {
    _username="$1"

    if [ -z "$_username" ]; then
        log_error "Usage: $0 adduser <username>"
        exit 1
    fi

    # Convert to uppercase for IBM i
    _username=$(echo "$_username" | tr '[:lower:]' '[:upper:]')

    echo "=============================================="
    echo "Add user $_username to group"
    echo "=============================================="
    echo ""

    # Check group exists
    if ! profile_exists "$GROUP_NAME"; then
        log_error "Group profile $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    check_authority

    # Check if user exists
    if ! profile_exists "$_username"; then
        log_error "User profile $_username does not exist"
        exit 1
    fi

    # Check if already a member
    if is_member "$_username" "$GROUP_NAME"; then
        log_skip "User $_username is already a member of $GROUP_NAME"
        return 0
    fi

    # Add to group
    add_user_to_group "$_username" "$GROUP_NAME"

    echo ""
    do_status
}

#----------------------------------------------------------------------
# Command: rmuser <name> [-d] - Remove user from group
#----------------------------------------------------------------------

do_rmuser() {
    _username="$1"
    _delete_user="$2"

    if [ -z "$_username" ]; then
        log_error "Usage: $0 rmuser <username> [-d]"
        log_error "  -d  Also delete the user profile"
        exit 1
    fi

    # Convert to uppercase for IBM i
    _username=$(echo "$_username" | tr '[:lower:]' '[:upper:]')

    echo "=============================================="
    echo "Remove user $_username from group"
    echo "=============================================="
    echo ""

    check_authority

    # Check if user is a member
    if ! is_member "$_username" "$GROUP_NAME"; then
        log_skip "User $_username is not a member of $GROUP_NAME"
    else
        remove_user_from_group "$_username"
    fi

    # Optionally delete the user
    if [ "$_delete_user" = "-d" ]; then
        echo ""
        delete_user "$_username"
    fi

    echo ""
    do_status
}

#----------------------------------------------------------------------
# Command: setmembers N - Set group to exactly N members
#----------------------------------------------------------------------

do_setmembers() {
    _num_members=$1

    if [ -z "$_num_members" ]; then
        log_error "Usage: $0 setmembers <count>"
        exit 1
    fi

    echo "=============================================="
    echo "Set group to exactly $_num_members members"
    echo "=============================================="
    echo ""

    # Check group exists
    if ! profile_exists "$GROUP_NAME"; then
        log_error "Group profile $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    check_authority

    _current_count=$(get_member_count "$GROUP_NAME")
    _highest_user=$(get_highest_user_number)

    log_info "Current members: $_current_count"
    log_info "Highest user: $_highest_user"
    log_info "Target members: $_num_members"
    echo ""

    # Need to create more users?
    if [ $_num_members -gt $_highest_user ]; then
        log_info "Need to create users $((_highest_user + 1)) to $_num_members"
        create_users_range $((_highest_user + 1)) $_num_members "$GROUP_NAME"
        echo ""
    fi

    # Need to add existing users to group?
    _i=1
    while [ $_i -le $_num_members ]; do
        _uname=$(printf "%s%03d" "$USER_PREFIX" $_i)
        if profile_exists "$_uname"; then
            if ! is_member "$_uname" "$GROUP_NAME"; then
                add_user_to_group "$_uname" "$GROUP_NAME"
            fi
        fi
        _i=$((_i + 1))
    done

    # Need to remove users from group?
    if [ $_num_members -lt $_highest_user ]; then
        log_warn "Removing users $((_num_members + 1)) to $_highest_user from group"
        _i=$((_num_members + 1))
        while [ $_i -le $_highest_user ]; do
            _uname=$(printf "%s%03d" "$USER_PREFIX" $_i)
            if profile_exists "$_uname"; then
                if is_member "$_uname" "$GROUP_NAME"; then
                    remove_user_from_group "$_uname"
                fi
            fi
            _i=$((_i + 1))
        done
        log_warn "Users still exist but are no longer in the group"
    fi

    echo ""
    log_info "Set members complete!"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: cleanup - Remove all test profiles
#----------------------------------------------------------------------

do_cleanup() {
    echo "=============================================="
    echo "Cleanup: Remove all test profiles"
    echo "=============================================="
    echo ""

    check_authority

    # Delete user profiles first (before group)
    log_info "Removing user profiles..."
    _i=1
    _removed=0
    _checked=0

    while [ $_i -le 999 ]; do
        _username=$(printf "%s%03d" "$USER_PREFIX" $_i)

        if profile_exists "$_username"; then
            delete_user "$_username"
            _removed=$((_removed + 1))
        fi

        _checked=$((_checked + 1))

        if [ $((_checked % 100)) -eq 0 ]; then
            log_info "  Checked $_checked, removed $_removed so far..."
        fi

        _i=$((_i + 1))
    done

    log_info "Removed $_removed user profiles"
    echo ""

    # Delete group profile
    log_info "Removing group profile..."
    delete_group "$GROUP_NAME"

    echo ""
    log_info "Cleanup complete!"
}

#----------------------------------------------------------------------
# Command: status - Show current status
#----------------------------------------------------------------------

do_status() {
    echo "=== IBM i Test Group Status ==="
    echo ""

    # Check group profile
    if profile_exists "$GROUP_NAME"; then
        echo "Group: $GROUP_NAME - EXISTS"

        # Get GID using QShell db2
        _gid=$(run_sql_query "SELECT GROUP_ID_NUMBER FROM QSYS2.USER_INFO WHERE USER_NAME = '$GROUP_NAME'" | grep -E '^\s*[0-9]+\s*$' | head -1 | tr -d ' ')
        if [ -n "$_gid" ]; then
            echo "GID: $_gid"
        fi

        # Count members
        _count=$(get_member_count "$GROUP_NAME")
        echo "Member count: $_count"

        # Show first few members using db2
        if [ "$_count" -gt 0 ] 2>/dev/null; then
            echo ""
            echo "Members (first 10):"
            run_sql_query "SELECT USER_PROFILE_NAME FROM QSYS2.GROUP_PROFILE_ENTRIES WHERE GROUP_PROFILE_NAME = '$GROUP_NAME' FETCH FIRST 10 ROWS ONLY" | grep -E '^\s*[A-Z]'
        fi
    else
        echo "Group: $GROUP_NAME - NOT FOUND"
        echo ""
        echo "Run: $0 setup [num_members]"
    fi

    echo ""

    # Count test users
    _highest=$(get_highest_user_number)
    echo "Test users found: $_highest"

    if [ $_highest -gt 0 ]; then
        echo "User range: ${USER_PREFIX}001 to ${USER_PREFIX}$(printf '%03d' $_highest)"
    fi
}

#----------------------------------------------------------------------
# Command: help
#----------------------------------------------------------------------

do_help() {
    cat << 'EOF'
IBM i PASE Test Group Setup Script
===================================

This script creates test user profiles and a group profile on IBM i
for testing the getgrent() and related PASE APIs.

PREREQUISITES:
  - Must run as a user with *SECADM special authority
  - Typically run as QSECOFR or equivalent
  - Run from PASE environment (SSH or QP2TERM)

USAGE:
  ./setup_test_groups_ibmi.sh <command> [options]

COMMANDS:
  setup [N]        Create group with N user profiles (default: 50)

  addusers N       Add N more users to existing group
                   Creates new users and adds to group

  adduser <name>   Add a specific existing user to group

  rmuser <name> [-d]
                   Remove user from group
                   -d also deletes the user profile

  setmembers N     Set group to exactly N members (users 1-N)
                   Can grow or shrink the member list

  cleanup          Remove all test profiles

  status           Show current test profile status

EXAMPLES:
  ./setup_test_groups_ibmi.sh setup 50          # Create with 50 members
  ./setup_test_groups_ibmi.sh addusers 25       # Add 25 more (now 75)
  ./setup_test_groups_ibmi.sh setmembers 100    # Grow to 100 members
  ./setup_test_groups_ibmi.sh setmembers 30     # Shrink to 30 members
  ./setup_test_groups_ibmi.sh rmuser ZTESTU005        # Remove from group
  ./setup_test_groups_ibmi.sh rmuser ZTESTU005 -d     # Remove AND delete
  ./setup_test_groups_ibmi.sh cleanup           # Remove everything

PROFILES CREATED:
  Group:  ZTEST_GRP (GID=59900)
  Users:  ZTESTU001, ZTESTU002, ... (UID=59901+)

AFTER SETUP:
  Run the getgrent test program (no special authority needed):
    ./getgrent_test -n              # Enumerate (non-reentrant)
    ./getgrent_test -a              # Show all groups

IBM i CL COMMANDS USED:
  CRTUSRPRF  - Create User Profile
  CHGUSRPRF  - Change User Profile (for group membership)
  DLTUSRPRF  - Delete User Profile
  CHKOBJ     - Check Object (to test if profile exists)

SQL QUERIES:
  Uses QShell db2 utility for SELECT queries on:
    QSYS2.USER_INFO            - User profile information
    QSYS2.GROUP_PROFILE_ENTRIES - Group membership

NOTE ON USER NAMES:
  IBM i profile names are limited to 10 characters and are
  stored/displayed in uppercase. PASE APIs return them in
  the case stored (typically uppercase).

EOF
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

case "${1:-help}" in
    setup)
        do_setup "$2"
        ;;
    addusers)
        do_addusers "$2"
        ;;
    adduser)
        do_adduser "$2"
        ;;
    rmuser)
        do_rmuser "$2" "$3"
        ;;
    setmembers)
        do_setmembers "$2"
        ;;
    cleanup)
        do_cleanup
        ;;
    status)
        do_status
        ;;
    help|-h|--help)
        do_help
        ;;
    *)
        log_error "Unknown command: $1"
        echo ""
        do_help
        exit 1
        ;;
esac
