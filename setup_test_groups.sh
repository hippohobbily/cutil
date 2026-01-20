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
#   ./setup_test_groups.sh setup [num_members]   # Create test group
#   ./setup_test_groups.sh cleanup               # Remove test groups/users
#   ./setup_test_groups.sh status                # Show current status
#   ./setup_test_groups.sh help                  # Show help
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
# Helper functions
#----------------------------------------------------------------------

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

check_root() {
    if [ "$(id -u)" != "0" ]; then
        log_error "This script must be run as root"
        log_error "Login as root directly: login root"
        exit 1
    fi
}

#----------------------------------------------------------------------
# User functions
#----------------------------------------------------------------------

create_user() {
    _username=$1
    _uid=$2

    # Check if exists
    lsuser "$_username" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        return 0
    fi

    mkuser id="$_uid" pgrp="staff" home="/tmp/$_username" shell="/bin/false" "$_username" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_error "Failed to create user $_username"
        return 1
    fi
    return 0
}

remove_user() {
    _username=$1

    lsuser "$_username" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        return 0
    fi

    rmuser -p "$_username" 2>/dev/null
    return $?
}

#----------------------------------------------------------------------
# Group functions
#----------------------------------------------------------------------

create_group() {
    _groupname=$1
    _gid=$2

    # Check if exists
    lsgroup "$_groupname" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        log_info "Group $_groupname already exists"
        return 0
    fi

    mkgroup id="$_gid" "$_groupname" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_error "Failed to create group $_groupname"
        return 1
    fi
    return 0
}

remove_group() {
    _groupname=$1

    lsgroup "$_groupname" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        return 0
    fi

    rmgroup "$_groupname" 2>/dev/null
    return $?
}

#----------------------------------------------------------------------
# Build member list (comma-separated)
#----------------------------------------------------------------------

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

#----------------------------------------------------------------------
# Setup
#----------------------------------------------------------------------

do_setup() {
    _num_members=${1:-$DEFAULT_MEMBERS}

    log_info "Creating test group with $_num_members members"
    log_info "Group: $GROUP_NAME (GID=$BASE_GID)"
    log_info "Users: ${USER_PREFIX}0001 - ${USER_PREFIX}$(printf '%04d' $_num_members)"
    echo ""

    # Create users
    log_info "Creating $_num_members test users..."
    _i=1
    while [ $_i -le $_num_members ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        _uid=$((BASE_UID + _i))
        create_user "$_uname" "$_uid"

        # Progress indicator
        if [ $((_i % 50)) -eq 0 ]; then
            log_info "  Created $_i users..."
        fi
        _i=$((_i + 1))
    done
    log_info "Users created."
    echo ""

    # Create group
    log_info "Creating group $GROUP_NAME..."
    create_group "$GROUP_NAME" "$BASE_GID"

    # Add members
    if [ $_num_members -gt 0 ]; then
        log_info "Adding members to group..."
        _members=$(build_member_list $_num_members)
        chgroup users="$_members" "$GROUP_NAME" 2>/dev/null
        if [ $? -ne 0 ]; then
            log_error "Failed to add members"
        fi
    fi

    echo ""
    log_info "Setup complete!"
    echo ""

    # Verify
    do_status
}

#----------------------------------------------------------------------
# Cleanup
#----------------------------------------------------------------------

do_cleanup() {
    log_info "Removing test groups and users..."
    echo ""

    # Remove group first
    log_info "Removing group $GROUP_NAME..."
    remove_group "$GROUP_NAME"

    # Remove users
    log_info "Removing test users..."
    _i=1
    _removed=0
    while [ $_i -le 2000 ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)

        lsuser "$_uname" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            remove_user "$_uname"
            _removed=$((_removed + 1))

            if [ $((_removed % 50)) -eq 0 ]; then
                log_info "  Removed $_removed users..."
            fi
        fi
        _i=$((_i + 1))
    done

    echo ""
    log_info "Cleanup complete! Removed $_removed users."
}

#----------------------------------------------------------------------
# Status
#----------------------------------------------------------------------

do_status() {
    echo "=== Test Group Status ==="
    echo ""

    # Check group
    lsgroup "$GROUP_NAME" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Group: $GROUP_NAME"
        lsgroup -a id users "$GROUP_NAME" 2>/dev/null

        # Count members
        _users=$(lsgroup -a users "$GROUP_NAME" 2>/dev/null | cut -d= -f2)
        if [ -n "$_users" ] && [ "$_users" != "" ]; then
            _count=$(echo "$_users" | tr ',' '\n' | wc -l)
            echo "Member count: $_count"
        else
            echo "Member count: 0"
        fi
    else
        echo "Group: $GROUP_NAME - NOT FOUND"
        echo ""
        echo "Run: $0 setup [num_members]"
    fi

    echo ""

    # Count test users
    _total=0
    _i=1
    while [ $_i -le 100 ]; do
        _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
        lsuser "$_uname" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            _total=$((_total + 1))
        fi
        _i=$((_i + 1))
    done

    # Check beyond 100 if we found some
    if [ $_total -eq 100 ]; then
        while [ $_i -le 2000 ]; do
            _uname=$(printf "%s%04d" "$USER_PREFIX" $_i)
            lsuser "$_uname" >/dev/null 2>&1
            if [ $? -eq 0 ]; then
                _total=$((_total + 1))
            else
                break
            fi
            _i=$((_i + 1))
        done
    fi

    echo "Test users found: $_total"
}

#----------------------------------------------------------------------
# Help
#----------------------------------------------------------------------

do_help() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  setup [N]    Create test group with N members (default: $DEFAULT_MEMBERS)"
    echo "  cleanup      Remove all test groups and users"
    echo "  status       Show current test group status"
    echo "  help         Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 setup           # Create group with $DEFAULT_MEMBERS members"
    echo "  $0 setup 100       # Create group with 100 members"
    echo "  $0 setup 500       # Create group with 500 members"
    echo "  $0 cleanup         # Remove all test entities"
    echo ""
    echo "Test entities:"
    echo "  Group:  $GROUP_NAME (GID=$BASE_GID)"
    echo "  Users:  ${USER_PREFIX}NNNN (UID=${BASE_UID}+N)"
    echo ""
    echo "After setup, run the test program (no root needed):"
    echo "  ./getgrent_test -e              # Enumerate groups"
    echo "  ./getgrent_test -g $GROUP_NAME  # Lookup test group"
    echo "  ./getgrent_test -p $GROUP_NAME  # Progressive buffer test"
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

case "${1:-help}" in
    setup)
        check_root
        do_setup "$2"
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
