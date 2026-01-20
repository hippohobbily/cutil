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
#   ./setup_test_groups_ibmi.sh setup [num_members]
#   ./setup_test_groups_ibmi.sh cleanup
#   ./setup_test_groups_ibmi.sh status
#   ./setup_test_groups_ibmi.sh help
#
# Note: The 'system' command is provided by IBM i PASE to run CL commands
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
HOME_BASE="/home"

#----------------------------------------------------------------------
# Helper functions
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

# Run a CL command and check result
run_cl() {
    _cmd="$1"
    _ignore_error="${2:-no}"

    # Use system command to run CL
    # -q = quiet (suppress command echo)
    # -s = return command status
    system -q "$_cmd" 2>/dev/null
    _rc=$?

    if [ $_rc -ne 0 ] && [ "$_ignore_error" != "yes" ]; then
        log_warn "CL command failed (rc=$_rc): $_cmd"
    fi

    return $_rc
}

# Check if user profile exists
profile_exists() {
    _profile="$1"
    # CHKOBJ checks if object exists
    system -q "CHKOBJ OBJ(QSYS/$_profile) OBJTYPE(*USRPRF)" 2>/dev/null
    return $?
}

#----------------------------------------------------------------------
# Check authority
#----------------------------------------------------------------------

check_authority() {
    # Try to check if we can create profiles
    # We do this by checking if current user has *SECADM
    log_info "Checking authority..."

    # Get current user
    _curuser=$(system -q "RTVJOBA CURUSER(?N)" 2>/dev/null | grep -o "'[^']*'" | tr -d "'")

    if [ -z "$_curuser" ]; then
        # Alternative: use whoami
        _curuser=$(whoami 2>/dev/null | tr '[:lower:]' '[:upper:]')
    fi

    log_info "Current user: $_curuser"
    log_info "Note: Requires *SECADM authority to create/delete user profiles"
    echo ""
}

#----------------------------------------------------------------------
# Create group profile
#----------------------------------------------------------------------

create_group() {
    _grpname="$1"
    _gid="$2"

    if profile_exists "$_grpname"; then
        log_info "Group profile $_grpname already exists"
        return 0
    fi

    log_info "Creating group profile: $_grpname (GID=$_gid)"

    # CRTUSRPRF with GID makes it a group profile
    # GRPPRF(*NONE) is required for group profiles
    run_cl "CRTUSRPRF USRPRF($_grpname) PASSWORD(*NONE) GID($_gid) GRPPRF(*NONE) TEXT('Test group for getgrent')"

    return $?
}

#----------------------------------------------------------------------
# Create user profile
#----------------------------------------------------------------------

create_user() {
    _username="$1"
    _uid="$2"
    _grpname="$3"

    if profile_exists "$_username"; then
        return 0
    fi

    # Create user with specified UID and group membership
    # HOMEDIR specifies IFS home directory
    # GRPPRF specifies primary group
    run_cl "CRTUSRPRF USRPRF($_username) PASSWORD(*NONE) UID($_uid) GRPPRF($_grpname) HOMEDIR('$HOME_BASE/$_username') TEXT('Test user for getgrent')" "yes"

    return $?
}

#----------------------------------------------------------------------
# Delete profile
#----------------------------------------------------------------------

delete_profile() {
    _profile="$1"

    if ! profile_exists "$_profile"; then
        return 0
    fi

    log_info "Deleting profile: $_profile"

    # DLTUSRPRF deletes user profile
    # OWNOBJOPT(*DLT) - delete owned objects
    run_cl "DLTUSRPRF USRPRF($_profile) OWNOBJOPT(*DLT)" "yes"

    return $?
}

#----------------------------------------------------------------------
# Setup
#----------------------------------------------------------------------

do_setup() {
    _num_members=${1:-$DEFAULT_MEMBERS}

    echo "=============================================="
    echo "IBM i Test Group Setup"
    echo "=============================================="
    echo ""
    echo "Group profile: $GROUP_NAME (GID=$BASE_GID)"
    echo "User profiles: ${USER_PREFIX}001 - ${USER_PREFIX}$(printf '%03d' $_num_members)"
    echo "Member count:  $_num_members"
    echo ""

    check_authority

    # Step 1: Create group profile
    log_info "Step 1: Creating group profile..."
    create_group "$GROUP_NAME" "$BASE_GID"
    if [ $? -ne 0 ]; then
        log_error "Failed to create group profile. Check authority."
        return 1
    fi
    echo ""

    # Step 2: Create user profiles
    log_info "Step 2: Creating $_num_members user profiles..."
    _i=1
    _created=0
    _failed=0

    while [ $_i -le $_num_members ]; do
        _username=$(printf "%s%03d" "$USER_PREFIX" $_i)
        _uid=$((BASE_UID + _i))

        create_user "$_username" "$_uid" "$GROUP_NAME"
        if [ $? -eq 0 ]; then
            _created=$((_created + 1))
        else
            _failed=$((_failed + 1))
        fi

        # Progress indicator
        if [ $((_i % 10)) -eq 0 ]; then
            echo "  Created $_i users..."
        fi

        _i=$((_i + 1))
    done

    echo ""
    log_info "Setup complete!"
    log_info "Created: $_created users, Failed: $_failed"
    echo ""

    # Show status
    do_status
}

#----------------------------------------------------------------------
# Cleanup
#----------------------------------------------------------------------

do_cleanup() {
    echo "=============================================="
    echo "IBM i Test Group Cleanup"
    echo "=============================================="
    echo ""

    check_authority

    # Step 1: Delete user profiles first (before group)
    log_info "Step 1: Removing user profiles..."
    _i=1
    _removed=0

    while [ $_i -le 999 ]; do
        _username=$(printf "%s%03d" "$USER_PREFIX" $_i)

        if profile_exists "$_username"; then
            delete_profile "$_username"
            _removed=$((_removed + 1))

            if [ $((_removed % 10)) -eq 0 ]; then
                echo "  Removed $_removed users..."
            fi
        fi

        _i=$((_i + 1))
    done

    log_info "Removed $_removed user profiles"
    echo ""

    # Step 2: Delete group profile
    log_info "Step 2: Removing group profile..."
    delete_profile "$GROUP_NAME"

    echo ""
    log_info "Cleanup complete!"
}

#----------------------------------------------------------------------
# Status
#----------------------------------------------------------------------

do_status() {
    echo "=============================================="
    echo "IBM i Test Group Status"
    echo "=============================================="
    echo ""

    # Check group profile
    if profile_exists "$GROUP_NAME"; then
        echo "Group: $GROUP_NAME - EXISTS"

        # Get GID using SQL
        echo ""
        echo "Group details (via SQL):"
        system -q "RUNSQL SQL('SELECT USER_NAME, GROUP_ID_NUMBER, TEXT_DESCRIPTION FROM QSYS2.USER_INFO WHERE USER_NAME = ''$GROUP_NAME''') OUTPUT(*PRINT)" 2>/dev/null

        # Count members using SQL
        echo ""
        echo "Group members:"
        system -q "RUNSQL SQL('SELECT COUNT(*) AS MEMBER_COUNT FROM QSYS2.GROUP_PROFILE_ENTRIES WHERE GROUP_PROFILE_NAME = ''$GROUP_NAME''') OUTPUT(*PRINT)" 2>/dev/null
    else
        echo "Group: $GROUP_NAME - NOT FOUND"
        echo ""
        echo "Run: $0 setup [num_members]"
    fi

    echo ""

    # Count test users
    _count=0
    _i=1
    while [ $_i -le 100 ]; do
        _username=$(printf "%s%03d" "$USER_PREFIX" $_i)
        if profile_exists "$_username"; then
            _count=$((_count + 1))
        fi
        _i=$((_i + 1))
    done

    # Continue counting if we found 100
    if [ $_count -eq 100 ]; then
        while [ $_i -le 999 ]; do
            _username=$(printf "%s%03d" "$USER_PREFIX" $_i)
            if profile_exists "$_username"; then
                _count=$((_count + 1))
            else
                break
            fi
            _i=$((_i + 1))
        done
    fi

    echo "Test users found: $_count"
}

#----------------------------------------------------------------------
# Help
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
  setup [N]    Create group and N user profiles (default: 50)
  cleanup      Remove all test profiles
  status       Show current test profile status
  help         Show this help

EXAMPLES:
  # Create test group with 50 members
  ./setup_test_groups_ibmi.sh setup

  # Create test group with 100 members
  ./setup_test_groups_ibmi.sh setup 100

  # Check status
  ./setup_test_groups_ibmi.sh status

  # Clean up all test profiles
  ./setup_test_groups_ibmi.sh cleanup

PROFILES CREATED:
  Group:  ZTEST_GRP (GID=59900)
  Users:  ZTESTU001, ZTESTU002, ... (UID=59901+)

AFTER SETUP:
  Run the getgrent test program (no special authority needed):
    ./getgrent_test -e              # Enumerate all groups
    ./getgrent_test -g ZTEST_GRP    # Lookup test group
    ./getgrent_test -p ZTEST_GRP    # Progressive buffer test

IBM i CL COMMANDS USED:
  CRTUSRPRF  - Create User Profile
  DLTUSRPRF  - Delete User Profile
  CHKOBJ     - Check Object (to test if profile exists)
  RUNSQL     - Run SQL (for status queries)

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
