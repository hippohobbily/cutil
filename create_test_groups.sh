#!/usr/bin/ksh
#
# create_test_groups.sh
#
# AIX Shell Script to Create Test Groups and Users for getgrent Testing
#
# This script creates various group configurations to test:
# - Small groups (few members)
# - Large groups (many members, approaching 2000 limit)
# - Groups with long names
# - Empty groups
# - Nested membership scenarios
#
# Usage: ./create_test_groups.sh [setup|cleanup|status]
#
# Must be run as root
#

# Configuration
PREFIX="tgrp"           # Test group prefix
USER_PREFIX="tusr"      # Test user prefix
BASE_GID=60000          # Starting GID for test groups
BASE_UID=60000          # Starting UID for test users

# Group sizes to test
SMALL_GROUP_SIZE=5
MEDIUM_GROUP_SIZE=50
LARGE_GROUP_SIZE=500
HUGE_GROUP_SIZE=1900    # Near the 2000 AIX limit

#----------------------------------------------------------------------
# Helper Functions
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
        exit 1
    fi
}

#----------------------------------------------------------------------
# User Creation Functions
#----------------------------------------------------------------------

# Create a single test user
# Usage: create_test_user <username> <uid>
create_test_user() {
    local username=$1
    local uid=$2

    if lsuser "$username" >/dev/null 2>&1; then
        log_info "User $username already exists"
        return 0
    fi

    log_info "Creating user: $username (UID=$uid)"
    mkuser id="$uid" pgrp="staff" home="/tmp/$username" "$username"
    if [ $? -ne 0 ]; then
        log_error "Failed to create user $username"
        return 1
    fi
    return 0
}

# Create multiple test users
# Usage: create_test_users <count> <start_index>
create_test_users() {
    local count=$1
    local start=${2:-1}
    local i

    log_info "Creating $count test users starting from index $start..."

    i=$start
    while [ $i -lt $((start + count)) ]; do
        # Format: tusr0001, tusr0002, etc.
        username=$(printf "%s%04d" "$USER_PREFIX" $i)
        uid=$((BASE_UID + i))
        create_test_user "$username" "$uid"
        i=$((i + 1))
    done

    log_info "Created $count test users"
}

#----------------------------------------------------------------------
# Group Creation Functions
#----------------------------------------------------------------------

# Create a single test group
# Usage: create_test_group <groupname> <gid>
create_test_group() {
    local groupname=$1
    local gid=$2

    if lsgroup "$groupname" >/dev/null 2>&1; then
        log_info "Group $groupname already exists"
        return 0
    fi

    log_info "Creating group: $groupname (GID=$gid)"
    mkgroup id="$gid" "$groupname"
    if [ $? -ne 0 ]; then
        log_error "Failed to create group $groupname"
        return 1
    fi
    return 0
}

# Add members to a group
# Usage: add_members_to_group <groupname> <member_list>
# member_list is comma-separated: user1,user2,user3
add_members_to_group() {
    local groupname=$1
    local members=$2

    log_info "Adding members to $groupname: $members"
    chgroup users="$members" "$groupname"
    if [ $? -ne 0 ]; then
        log_error "Failed to add members to $groupname"
        return 1
    fi
    return 0
}

# Generate comma-separated list of usernames
# Usage: generate_member_list <count> <start_index>
generate_member_list() {
    local count=$1
    local start=${2:-1}
    local list=""
    local i

    i=$start
    while [ $i -lt $((start + count)) ]; do
        username=$(printf "%s%04d" "$USER_PREFIX" $i)
        if [ -z "$list" ]; then
            list="$username"
        else
            list="$list,$username"
        fi
        i=$((i + 1))
    done

    echo "$list"
}

#----------------------------------------------------------------------
# Test Group Configurations
#----------------------------------------------------------------------

# Create empty group (no members)
create_empty_group() {
    log_info "=== Creating Empty Group ==="
    create_test_group "${PREFIX}_empty" $((BASE_GID + 1))
}

# Create small group
create_small_group() {
    log_info "=== Creating Small Group ($SMALL_GROUP_SIZE members) ==="

    # Ensure users exist
    create_test_users $SMALL_GROUP_SIZE 1

    # Create group and add members
    create_test_group "${PREFIX}_small" $((BASE_GID + 2))
    members=$(generate_member_list $SMALL_GROUP_SIZE 1)
    add_members_to_group "${PREFIX}_small" "$members"
}

# Create medium group
create_medium_group() {
    log_info "=== Creating Medium Group ($MEDIUM_GROUP_SIZE members) ==="

    # Ensure users exist
    create_test_users $MEDIUM_GROUP_SIZE 1

    # Create group and add members
    create_test_group "${PREFIX}_medium" $((BASE_GID + 3))
    members=$(generate_member_list $MEDIUM_GROUP_SIZE 1)
    add_members_to_group "${PREFIX}_medium" "$members"
}

# Create large group
create_large_group() {
    log_info "=== Creating Large Group ($LARGE_GROUP_SIZE members) ==="

    # Ensure users exist
    create_test_users $LARGE_GROUP_SIZE 1

    # Create group and add members
    create_test_group "${PREFIX}_large" $((BASE_GID + 4))
    members=$(generate_member_list $LARGE_GROUP_SIZE 1)
    add_members_to_group "${PREFIX}_large" "$members"
}

# Create huge group (near AIX 2000 member limit)
create_huge_group() {
    log_info "=== Creating Huge Group ($HUGE_GROUP_SIZE members) ==="
    log_info "This may take a while..."

    # Ensure users exist
    create_test_users $HUGE_GROUP_SIZE 1

    # Create group
    create_test_group "${PREFIX}_huge" $((BASE_GID + 5))

    # For huge groups, we may need to add members in batches
    # due to command line length limits
    local batch_size=100
    local added=0
    local i=1

    while [ $added -lt $HUGE_GROUP_SIZE ]; do
        remaining=$((HUGE_GROUP_SIZE - added))
        if [ $remaining -lt $batch_size ]; then
            batch_size=$remaining
        fi

        members=$(generate_member_list $batch_size $i)

        if [ $added -eq 0 ]; then
            # First batch: set members
            chgroup users="$members" "${PREFIX}_huge"
        else
            # Subsequent batches: append using chgrpmem
            chgrpmem -m + $members "${PREFIX}_huge"
        fi

        added=$((added + batch_size))
        i=$((i + batch_size))
        log_info "Added $added of $HUGE_GROUP_SIZE members..."
    done
}

# Create groups with various name lengths
create_named_groups() {
    log_info "=== Creating Groups with Various Name Lengths ==="

    # Short name (traditional 8-char limit)
    create_test_group "tg_short" $((BASE_GID + 10))

    # Medium name
    create_test_group "testgroup_medium_name" $((BASE_GID + 11))

    # Long name (AIX 5.3+ supports up to 255 chars)
    create_test_group "testgroup_with_a_very_long_name_for_testing_purposes" $((BASE_GID + 12))

    # Add a few members to each
    create_test_users 3 1
    members=$(generate_member_list 3 1)
    add_members_to_group "tg_short" "$members"
    add_members_to_group "testgroup_medium_name" "$members"
    add_members_to_group "testgroup_with_a_very_long_name_for_testing_purposes" "$members"
}

#----------------------------------------------------------------------
# Cleanup Functions
#----------------------------------------------------------------------

cleanup_test_users() {
    log_info "=== Removing Test Users ==="

    # Find and remove all test users
    lsuser -a ALL 2>/dev/null | while read username rest; do
        case "$username" in
            ${USER_PREFIX}*)
                log_info "Removing user: $username"
                rmuser -p "$username" 2>/dev/null
                ;;
        esac
    done
}

cleanup_test_groups() {
    log_info "=== Removing Test Groups ==="

    # Find and remove all test groups
    lsgroup -a ALL 2>/dev/null | while read groupname rest; do
        case "$groupname" in
            ${PREFIX}*|tg_short|testgroup_*)
                log_info "Removing group: $groupname"
                rmgroup "$groupname" 2>/dev/null
                ;;
        esac
    done
}

#----------------------------------------------------------------------
# Status Functions
#----------------------------------------------------------------------

show_status() {
    log_info "=== Test Groups Status ==="

    echo ""
    echo "Test Groups:"
    echo "------------"
    lsgroup -a id users ALL 2>/dev/null | while read line; do
        groupname=$(echo "$line" | cut -d' ' -f1)
        case "$groupname" in
            ${PREFIX}*|tg_short|testgroup_*)
                echo "$line"
                # Count members
                members=$(lsgroup -a users "$groupname" 2>/dev/null | cut -d'=' -f2)
                if [ -n "$members" ] && [ "$members" != "" ]; then
                    count=$(echo "$members" | tr ',' '\n' | wc -l)
                    echo "  -> Member count: $count"
                else
                    echo "  -> Member count: 0"
                fi
                ;;
        esac
    done

    echo ""
    echo "Test Users (first 10):"
    echo "----------------------"
    lsuser -a id pgrp ALL 2>/dev/null | grep "^${USER_PREFIX}" | head -10

    # Count total test users
    total_users=$(lsuser ALL 2>/dev/null | grep "^${USER_PREFIX}" | wc -l)
    echo "..."
    echo "Total test users: $total_users"
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

main() {
    check_root

    case "${1:-setup}" in
        setup)
            log_info "Setting up test groups and users..."
            echo ""

            create_empty_group
            echo ""

            create_small_group
            echo ""

            create_medium_group
            echo ""

            create_named_groups
            echo ""

            # Uncomment these for more extensive testing:
            # create_large_group
            # echo ""
            # create_huge_group
            # echo ""

            log_info "Setup complete!"
            echo ""
            show_status
            ;;

        large)
            log_info "Creating large test groups..."
            create_large_group
            ;;

        huge)
            log_info "Creating huge test group (near 2000 member limit)..."
            create_huge_group
            ;;

        cleanup)
            log_info "Cleaning up test groups and users..."
            cleanup_test_groups
            cleanup_test_users
            log_info "Cleanup complete!"
            ;;

        status)
            show_status
            ;;

        *)
            echo "Usage: $0 [setup|large|huge|cleanup|status]"
            echo ""
            echo "  setup   - Create basic test groups (empty, small, medium)"
            echo "  large   - Create large test group ($LARGE_GROUP_SIZE members)"
            echo "  huge    - Create huge test group ($HUGE_GROUP_SIZE members)"
            echo "  cleanup - Remove all test groups and users"
            echo "  status  - Show current test group configuration"
            exit 1
            ;;
    esac
}

main "$@"
