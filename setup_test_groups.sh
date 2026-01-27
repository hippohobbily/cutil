#!/bin/ksh
#
# setup_test_groups.sh
#
# AIX Shell Script to Create/Remove Test Groups and Users
# Uses only AIX system shipped commands (mkuser, rmuser, mkgroup, rmgroup, chgroup)
#
# Must be run as root (login as root directly if su is not available)
#
# Usage:
#   ./setup_test_groups.sh setup [N]        # Create group with N members (replaces)
#   ./setup_test_groups.sh addusers N       # Add N more users to existing group
#   ./setup_test_groups.sh adduser <name>   # Add specific user to group
#   ./setup_test_groups.sh rmuser <name>    # Remove user from group (optionally delete)
#   ./setup_test_groups.sh setmembers N     # Set group to exactly N members
#   ./setup_test_groups.sh cleanup          # Remove all test groups/users
#   ./setup_test_groups.sh status           # Show current status
#   ./setup_test_groups.sh help             # Show help
#

# Configuration - safe prefix to avoid conflicts
PREFIX="ztest"
GROUP_NAME="ztest_grp"
USER_PREFIX="ztest_u"
BASE_GID=59900
BASE_UID=59900

# Default number of members
DEFAULT_MEMBERS=50

#----------------------------------------------------------------------
# Logging functions (always verbose)
#----------------------------------------------------------------------

log_info() {
    echo "[INFO] $1"
}

log_warn() {
    echo "[WARN] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
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

check_root() {
    if [ "$(id -u)" != "0" ]; then
        log_error "This script must be run as root"
        log_error "Login as root directly: login root"
        exit 1
    fi
}

#----------------------------------------------------------------------
# Group member query functions
#----------------------------------------------------------------------

# Get current group members as comma-separated string
# Returns empty string if group doesn't exist or has no members
get_current_members() {
    _grp=$1
    lsgroup "$_grp" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo ""
        return 1
    fi

    _raw=$(lsgroup -a users "$_grp" 2>/dev/null)
    # Format: "ztest_grp users=user1,user2,user3"
    _members=$(echo "$_raw" | sed 's/.*users=//')

    # Handle empty case
    if [ "$_members" = "$_grp" ]; then
        echo ""
    else
        echo "$_members"
    fi
}

# Get count of current group members
get_member_count() {
    _members=$(get_current_members "$1")
    if [ -z "$_members" ]; then
        echo "0"
    else
        echo "$_members" | tr ',' '\n' | wc -l | tr -d ' '
    fi
}

# Check if a user is in the group
is_member() {
    _user=$1
    _members=$(get_current_members "$GROUP_NAME")

    if [ -z "$_members" ]; then
        return 1
    fi

    echo ",$_members," | grep -q ",$_user,"
    return $?
}

# Get the highest numbered test user that exists
get_highest_user_number() {
    _highest=0
    _i=1

    # Quick scan first 100
    while [ $_i -le 100 ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        lsuser "$_uname" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            _highest=$_i
        fi
        _i=$((_i + 1))
    done

    # If found 100, continue scanning
    if [ $_highest -eq 100 ]; then
        while [ $_i -le 2000 ]; do
            _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
            lsuser "$_uname" >/dev/null 2>&1
            if [ $? -eq 0 ]; then
                _highest=$_i
            else
                break
            fi
            _i=$((_i + 1))
        done
    fi

    echo "$_highest"
}

#----------------------------------------------------------------------
# User functions (verbose)
#----------------------------------------------------------------------

# Create a single user - verbose
create_user() {
    _username=$1
    _uid=$2

    # Check if exists
    lsuser "$_username" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        log_skip "User $_username already exists (UID=$_uid)"
        return 0
    fi

    mkuser id="$_uid" pgrp="staff" home="/tmp/$_username" shell="/bin/false" "$_username" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_error "Failed to create user $_username"
        return 1
    fi

    log_create "User $_username (UID=$_uid)"
    return 0
}

# Remove a single user - verbose
delete_user() {
    _username=$1

    lsuser "$_username" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_skip "User $_username does not exist"
        return 0
    fi

    rmuser -p "$_username" 2>/dev/null
    if [ $? -eq 0 ]; then
        log_delete "User $_username"
        return 0
    else
        log_error "Failed to delete user $_username"
        return 1
    fi
}

# Create multiple users in a range
create_users_range() {
    _start=$1
    _end=$2
    _created=0
    _skipped=0

    log_info "Creating users ${USER_PREFIX}$(printf '%04d' $_start) to ${USER_PREFIX}$(printf '%04d' $_end)..."

    _i=$_start
    while [ $_i -le $_end ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        _uid=$((BASE_UID + _i))

        lsuser "$_uname" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            log_skip "User $_uname already exists"
            _skipped=$((_skipped + 1))
        else
            mkuser id="$_uid" pgrp="staff" home="/tmp/$_uname" shell="/bin/false" "$_uname" 2>/dev/null
            if [ $? -eq 0 ]; then
                log_create "User $_uname (UID=$_uid)"
                _created=$((_created + 1))
            else
                log_error "Failed to create user $_uname"
            fi
        fi

        _i=$((_i + 1))
    done

    log_info "Users: $_created created, $_skipped skipped (already existed)"
}

#----------------------------------------------------------------------
# Group functions (verbose)
#----------------------------------------------------------------------

# Create group if not exists
create_group() {
    _groupname=$1
    _gid=$2

    lsgroup "$_groupname" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        log_skip "Group $_groupname already exists"
        return 0
    fi

    mkgroup id="$_gid" "$_groupname" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_error "Failed to create group $_groupname"
        return 1
    fi

    log_create "Group $_groupname (GID=$_gid)"
    return 0
}

# Delete group
delete_group() {
    _groupname=$1

    lsgroup "$_groupname" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_skip "Group $_groupname does not exist"
        return 0
    fi

    rmgroup "$_groupname" 2>/dev/null
    if [ $? -eq 0 ]; then
        log_delete "Group $_groupname"
        return 0
    else
        log_error "Failed to delete group $_groupname"
        return 1
    fi
}

# Set group members (full replacement)
set_group_members() {
    _groupname=$1
    _members=$2

    if [ -z "$_members" ]; then
        log_info "Setting group $_groupname to have no members"
        chgroup users="" "$_groupname" 2>/dev/null
    else
        _count=$(echo "$_members" | tr ',' '\n' | wc -l | tr -d ' ')
        log_info "Setting group $_groupname members (count: $_count)"
        chgroup users="$_members" "$_groupname" 2>/dev/null
    fi

    if [ $? -ne 0 ]; then
        log_error "Failed to set group members"
        return 1
    fi
    return 0
}

#----------------------------------------------------------------------
# Build member list (comma-separated)
#----------------------------------------------------------------------

# Build list from 1 to N
build_member_list() {
    _count=$1
    _list=""
    _i=1

    while [ $_i -le $_count ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        if [ -z "$_list" ]; then
            _list="$_uname"
        else
            _list="$_list,$_uname"
        fi
        _i=$((_i + 1))
    done

    echo "$_list"
}

# Build list from start to end
build_member_list_range() {
    _start=$1
    _end=$2
    _list=""

    _i=$_start
    while [ $_i -le $_end ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        if [ -z "$_list" ]; then
            _list="$_uname"
        else
            _list="$_list,$_uname"
        fi
        _i=$((_i + 1))
    done

    echo "$_list"
}

#----------------------------------------------------------------------
# Command: setup [N] - Create group with N members (replaces existing)
#----------------------------------------------------------------------

do_setup() {
    _num_members=${1:-$DEFAULT_MEMBERS}

    echo "========================================"
    echo "Setup: Create group with $_num_members members"
    echo "========================================"
    echo ""
    log_info "Group: $GROUP_NAME (GID=$BASE_GID)"
    log_info "Users: ${USER_PREFIX}0001 to ${USER_PREFIX}$(printf '%04d' $_num_members)"
    echo ""

    # Create users
    create_users_range 1 $_num_members
    echo ""

    # Create group
    create_group "$GROUP_NAME" "$BASE_GID"

    # Set members (full replacement)
    if [ $_num_members -gt 0 ]; then
        _members=$(build_member_list $_num_members)
        set_group_members "$GROUP_NAME" "$_members"
    else
        set_group_members "$GROUP_NAME" ""
    fi

    echo ""
    log_info "Setup complete!"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: addusers N - Add N more users to existing group
#----------------------------------------------------------------------

do_addusers() {
    _add_count=$1

    if [ -z "$_add_count" ] || [ "$_add_count" -le 0 ]; then
        log_error "Usage: $0 addusers <count>"
        log_error "  count must be a positive number"
        exit 1
    fi

    echo "========================================"
    echo "Add $_add_count more users to group"
    echo "========================================"
    echo ""

    # Check group exists
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_error "Group $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    # Get current state
    _current_members=$(get_current_members "$GROUP_NAME")
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
    create_users_range $_start $_end
    echo ""

    # Build new member list and append
    _new_members=$(build_member_list_range $_start $_end)

    if [ -z "$_current_members" ]; then
        _all_members="$_new_members"
    else
        _all_members="$_current_members,$_new_members"
    fi

    # Update group
    set_group_members "$GROUP_NAME" "$_all_members"

    echo ""
    log_info "Added $_add_count users to group"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: adduser <name> - Add specific user to group
#----------------------------------------------------------------------

do_adduser() {
    _username=$1

    if [ -z "$_username" ]; then
        log_error "Usage: $0 adduser <username>"
        exit 1
    fi

    echo "========================================"
    echo "Add user $_username to group"
    echo "========================================"
    echo ""

    # Check group exists
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_error "Group $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    # Check if user exists
    lsuser "$_username" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_error "User $_username does not exist"
        exit 1
    fi

    # Check if already a member
    if is_member "$_username"; then
        log_skip "User $_username is already a member of $GROUP_NAME"
        return 0
    fi

    # Get current members and append
    _current_members=$(get_current_members "$GROUP_NAME")

    if [ -z "$_current_members" ]; then
        _all_members="$_username"
    else
        _all_members="$_current_members,$_username"
    fi

    # Update group
    chgroup users="$_all_members" "$GROUP_NAME" 2>/dev/null
    if [ $? -eq 0 ]; then
        log_add "User $_username to group $GROUP_NAME"
    else
        log_error "Failed to add user to group"
        exit 1
    fi

    echo ""
    do_status
}

#----------------------------------------------------------------------
# Command: rmuser <name> [-d] - Remove user from group (optionally delete)
#----------------------------------------------------------------------

do_rmuser() {
    _username=$1
    _delete_user=$2

    if [ -z "$_username" ]; then
        log_error "Usage: $0 rmuser <username> [-d]"
        log_error "  -d  Also delete the user account"
        exit 1
    fi

    echo "========================================"
    echo "Remove user $_username from group"
    echo "========================================"
    echo ""

    # Check group exists
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_error "Group $GROUP_NAME does not exist"
        exit 1
    fi

    # Check if user is a member
    if ! is_member "$_username"; then
        log_skip "User $_username is not a member of $GROUP_NAME"
    else
        # Get current members and remove this user
        _current_members=$(get_current_members "$GROUP_NAME")

        # Remove user from comma-separated list
        _new_members=$(echo "$_current_members" | tr ',' '\n' | grep -v "^${_username}$" | tr '\n' ',' | sed 's/,$//')

        # Update group
        chgroup users="$_new_members" "$GROUP_NAME" 2>/dev/null
        if [ $? -eq 0 ]; then
            log_remove "User $_username from group $GROUP_NAME"
        else
            log_error "Failed to remove user from group"
            exit 1
        fi
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

    echo "========================================"
    echo "Set group to exactly $_num_members members"
    echo "========================================"
    echo ""

    # Check group exists
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log_error "Group $GROUP_NAME does not exist"
        log_error "Run '$0 setup' first"
        exit 1
    fi

    _current_count=$(get_member_count "$GROUP_NAME")
    log_info "Current members: $_current_count"
    log_info "Target members: $_num_members"
    echo ""

    if [ $_num_members -eq $_current_count ]; then
        log_info "Already at target count, no changes needed"
        return 0
    fi

    # Ensure users exist
    if [ $_num_members -gt 0 ]; then
        _highest=$(get_highest_user_number)
        if [ $_num_members -gt $_highest ]; then
            log_info "Need to create users $((_highest + 1)) to $_num_members"
            create_users_range $((_highest + 1)) $_num_members
            echo ""
        fi
    fi

    # Build and set member list
    if [ $_num_members -eq 0 ]; then
        set_group_members "$GROUP_NAME" ""
    else
        _members=$(build_member_list $_num_members)
        set_group_members "$GROUP_NAME" "$_members"
    fi

    if [ $_num_members -lt $_current_count ]; then
        log_warn "Reduced from $_current_count to $_num_members members"
        log_warn "Users still exist but are no longer in the group"
    fi

    echo ""
    log_info "Set members complete!"
    echo ""

    do_status
}

#----------------------------------------------------------------------
# Command: cleanup - Remove all test groups and users
#----------------------------------------------------------------------

do_cleanup() {
    echo "========================================"
    echo "Cleanup: Remove all test entities"
    echo "========================================"
    echo ""

    # Remove group first
    delete_group "$GROUP_NAME"
    echo ""

    # Remove users
    log_info "Removing test users..."
    _i=1
    _removed=0
    _checked=0
    while [ $_i -le 2000 ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)

        lsuser "$_uname" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            delete_user "$_uname"
            _removed=$((_removed + 1))
        fi

        _checked=$((_checked + 1))

        # Progress every 100 checked (not removed)
        if [ $((_checked % 500)) -eq 0 ]; then
            log_info "  Checked $_checked, removed $_removed so far..."
        fi

        _i=$((_i + 1))
    done

    echo ""
    log_info "Cleanup complete! Removed $_removed users."
}

#----------------------------------------------------------------------
# Command: status - Show current status
#----------------------------------------------------------------------

do_status() {
    echo "=== Test Group Status ==="
    echo ""

    # Check group
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        _gid=$(lsgroup -a id "$GROUP_NAME" 2>/dev/null | cut -d= -f2)
        echo "Group: $GROUP_NAME (GID=$_gid)"

        _count=$(get_member_count "$GROUP_NAME")
        echo "Member count: $_count"

        # Show first few and last few members if many
        if [ $_count -gt 0 ] && [ $_count -le 10 ]; then
            _members=$(get_current_members "$GROUP_NAME")
            echo "Members: $_members"
        elif [ $_count -gt 10 ]; then
            _members=$(get_current_members "$GROUP_NAME")
            _first=$(echo "$_members" | cut -d',' -f1-3)
            _last=$(echo "$_members" | tr ',' '\n' | tail -3 | tr '\n' ',' | sed 's/,$//')
            echo "Members: $_first ... $_last"
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
        echo "User range: ${USER_PREFIX}0001 to ${USER_PREFIX}$(printf '%04d' $_highest)"
    fi
}

#----------------------------------------------------------------------
# Command: help
#----------------------------------------------------------------------

do_help() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  setup [N]        Create test group with N members (default: $DEFAULT_MEMBERS)"
    echo "                   Replaces existing member list with users 1-N"
    echo ""
    echo "  addusers N       Add N more users to existing group"
    echo "                   Creates new users and appends to member list"
    echo ""
    echo "  adduser <name>   Add a specific existing user to group"
    echo ""
    echo "  rmuser <name> [-d]"
    echo "                   Remove user from group"
    echo "                   -d also deletes the user account"
    echo ""
    echo "  setmembers N     Set group to exactly N members (users 1-N)"
    echo "                   Can grow or shrink the member list"
    echo ""
    echo "  cleanup          Remove all test groups and users"
    echo ""
    echo "  status           Show current test group status"
    echo ""
    echo "Examples:"
    echo "  $0 setup 50          # Create group with 50 members"
    echo "  $0 addusers 25       # Add 25 more (now 75 members)"
    echo "  $0 setmembers 100    # Grow to 100 members"
    echo "  $0 setmembers 30     # Shrink to 30 members"
    echo "  $0 rmuser ztest_u0005      # Remove user 5 from group"
    echo "  $0 rmuser ztest_u0005 -d   # Remove from group AND delete user"
    echo "  $0 cleanup           # Remove everything"
    echo ""
    echo "Test entities:"
    echo "  Group:  $GROUP_NAME (GID=$BASE_GID)"
    echo "  Users:  ${USER_PREFIX}NNNN (UID=${BASE_UID}+N)"
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

case "${1:-help}" in
    setup)
        check_root
        do_setup "$2"
        ;;
    addusers)
        check_root
        do_addusers "$2"
        ;;
    adduser)
        check_root
        do_adduser "$2"
        ;;
    rmuser)
        check_root
        do_rmuser "$2" "$3"
        ;;
    setmembers)
        check_root
        do_setmembers "$2"
        ;;
    cleanup)
        check_root
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
