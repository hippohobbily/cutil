#!/QOpenSys/usr/bin/sh
#
# listptf.sh - List PTFs installed on IBM i
#
# Queries PTF information using SQL (QSYS2.PTF_INFO) and outputs
# to a file in the home directory.
#
# Usage:
#   ./listptf.sh                    # List all PTFs
#   ./listptf.sh 5770SS1            # List PTFs for specific product
#   ./listptf.sh 5770SS1 5770WDS    # List PTFs for multiple products
#   ./listptf.sh -o /tmp/ptfs.txt   # Specify output file
#   ./listptf.sh -v                 # Verbose (show SQL commands)
#   ./listptf.sh -h                 # Show help
#
# Output file: ~/ptf_list.txt (default)
#
# Requires: *ALLOBJ or appropriate authority to query PTF info
#

#----------------------------------------------------------------------
# Configuration
#----------------------------------------------------------------------

# Default output file
OUTPUT_FILE="${HOME:-/QOpenSys/home/$USER}/ptf_list.txt"

# Products to query (empty = all)
PRODUCTS=""

# Verbose mode (show SQL commands)
VERBOSE=0

# All columns mode
ALL_COLUMNS=0

# Summary mode
SUMMARY=0

#----------------------------------------------------------------------
# Logging Functions
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

log_debug() {
    if [ $VERBOSE -eq 1 ]; then
        echo "[DEBUG] $1"
    fi
}

log_cmd() {
    # Log the exact command being executed
    if [ $VERBOSE -eq 1 ]; then
        echo "[CMD] $1"
    fi
}

log_sql() {
    # Log SQL statement being executed
    if [ $VERBOSE -eq 1 ]; then
        echo "[SQL] $1"
    fi
}

#----------------------------------------------------------------------
# Usage
#----------------------------------------------------------------------

usage() {
    cat << 'EOF'
listptf.sh - List PTFs installed on IBM i

USAGE:
    ./listptf.sh [options] [product_id ...]

OPTIONS:
    -o FILE     Write output to FILE (default: ~/ptf_list.txt)
    -a          Show all columns (more PTF details)
    -s          Summary only (count by status)
    -v          Verbose mode (show exact SQL commands executed)
    -h          Show this help

ARGUMENTS:
    product_id  One or more product IDs to filter (e.g., 5770SS1)
                If not specified, lists PTFs for all products

EXAMPLES:
    ./listptf.sh                        # All PTFs, default output
    ./listptf.sh 5770SS1                # Only OS PTFs
    ./listptf.sh 5770SS1 5770DG1        # OS and HTTP Server PTFs
    ./listptf.sh -o /tmp/mylist.txt     # Custom output file
    ./listptf.sh -s                     # Summary counts only
    ./listptf.sh -a 5770SS1             # All columns for OS PTFs
    ./listptf.sh -v 5770SS1             # Verbose mode showing SQL

OUTPUT FORMAT:
    PTF_ID    PRODUCT    STATUS              ACTION_PENDING
    SI12345   5770SS1    PERMANENTLY APPLIED NONE
    SI12346   5770SS1    TEMPORARILY APPLIED NONE
    ...

STATUS VALUES:
    NOT LOADED              - PTF save file exists but not loaded
    LOADED                  - Loaded but not applied
    APPLIED                 - Temporarily applied (removed at IPL)
    PERMANENTLY APPLIED     - Permanently applied
    SUPERSEDED              - Replaced by newer PTF
    DAMAGED                 - PTF is damaged

SQL TABLE:
    QSYS2.PTF_INFO - PTF metadata and status

EXECUTION METHOD:
    /QOpenSys/usr/bin/qsh -c "db2 \"<sql>\""

EOF
}

#----------------------------------------------------------------------
# Run SQL query using QShell db2 utility
#----------------------------------------------------------------------

run_sql() {
    _sql="$1"
    _db2_cmd="/QOpenSys/usr/bin/qsh -c \"db2 \\\"$_sql\\\"\""

    log_sql "$_sql"
    log_cmd "$_db2_cmd"

    _result=$(/QOpenSys/usr/bin/qsh -c "db2 \"$_sql\"" 2>&1)
    _rc=$?

    if [ $VERBOSE -eq 1 ]; then
        log_debug "SQL return code: $_rc"
        if [ -n "$_result" ]; then
            _lines=$(echo "$_result" | wc -l | tr -d ' ')
            log_debug "SQL result: $_lines lines returned"
        fi
    fi

    echo "$_result"
    return $_rc
}

#----------------------------------------------------------------------
# Build WHERE clause for product filter
#----------------------------------------------------------------------

build_product_filter() {
    if [ -z "$PRODUCTS" ]; then
        log_debug "No product filter (querying ALL products)"
        echo ""
        return
    fi

    _filter="PTF_PRODUCT_ID IN ("
    _first=1

    for _prod in $PRODUCTS; do
        if [ $_first -eq 1 ]; then
            _filter="${_filter}'${_prod}'"
            _first=0
        else
            _filter="${_filter}, '${_prod}'"
        fi
    done

    _filter="${_filter})"
    log_debug "Product filter: $_filter"
    echo "$_filter"
}

#----------------------------------------------------------------------
# List PTFs (main query)
#----------------------------------------------------------------------

list_ptfs() {
    _filter=$(build_product_filter)

    if [ -n "$_filter" ]; then
        _where="WHERE $_filter"
    else
        _where=""
    fi

    _sql="SELECT
    PTF_IDENTIFIER AS PTF_ID,
    PTF_PRODUCT_ID AS PRODUCT,
    PTF_LOADED_STATUS AS STATUS,
    PTF_IPL_ACTION AS ACTION_PENDING,
    PTF_SAVE_FILE AS SAVE_FILE,
    PTF_IPL_REQUIRED AS IPL_REQ
FROM QSYS2.PTF_INFO
$_where
ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER"

    log_info "Executing PTF list query..."
    run_sql "$_sql"
}

#----------------------------------------------------------------------
# List PTFs (all columns)
#----------------------------------------------------------------------

list_ptfs_all_columns() {
    _filter=$(build_product_filter)

    if [ -n "$_filter" ]; then
        _where="WHERE $_filter"
    else
        _where=""
    fi

    _sql="SELECT
    PTF_IDENTIFIER,
    PTF_PRODUCT_ID,
    PTF_LOADED_STATUS,
    PTF_IPL_ACTION,
    PTF_ACTION_PENDING,
    PTF_ACTION_REQUIRED,
    PTF_IPL_REQUIRED,
    PTF_CREATION_TIMESTAMP,
    PTF_STATUS_TIMESTAMP,
    PTF_SAVE_FILE,
    PTF_PRODUCT_OPTION,
    PTF_PRODUCT_LOAD,
    PTF_RELEASE_LEVEL,
    PTF_MINIMUM_LEVEL,
    PTF_MAXIMUM_LEVEL,
    PTF_SUPERSEDED_BY_PTF
FROM QSYS2.PTF_INFO
$_where
ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER"

    log_info "Executing PTF list query (all columns)..."
    run_sql "$_sql"
}

#----------------------------------------------------------------------
# Summary (count by status)
#----------------------------------------------------------------------

list_ptfs_summary() {
    _filter=$(build_product_filter)

    if [ -n "$_filter" ]; then
        _where="WHERE $_filter"
    else
        _where=""
    fi

    _sql="SELECT
    PTF_PRODUCT_ID AS PRODUCT,
    PTF_LOADED_STATUS AS STATUS,
    COUNT(*) AS COUNT
FROM QSYS2.PTF_INFO
$_where
GROUP BY PTF_PRODUCT_ID, PTF_LOADED_STATUS
ORDER BY PTF_PRODUCT_ID, PTF_LOADED_STATUS"

    log_info "Executing PTF summary query..."
    run_sql "$_sql"
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Parse options
while getopts "o:asvh" opt; do
    case $opt in
        o)
            OUTPUT_FILE="$OPTARG"
            ;;
        a)
            ALL_COLUMNS=1
            ;;
        s)
            SUMMARY=1
            ;;
        v)
            VERBOSE=1
            ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

shift $((OPTIND - 1))

# Remaining arguments are product IDs
PRODUCTS="$*"

# Header
echo "=============================================="
echo "IBM i PTF List"
echo "=============================================="
echo ""
echo "Date:       $(date)"
echo "System:     $(hostname 2>/dev/null || echo 'unknown')"
echo "Output:     $OUTPUT_FILE"
echo "Verbose:    $([ $VERBOSE -eq 1 ] && echo 'YES' || echo 'NO')"

if [ -n "$PRODUCTS" ]; then
    echo "Products:   $PRODUCTS"
else
    echo "Products:   ALL"
fi

if [ $SUMMARY -eq 1 ]; then
    echo "Mode:       Summary (count by status)"
elif [ $ALL_COLUMNS -eq 1 ]; then
    echo "Mode:       All columns"
else
    echo "Mode:       Standard"
fi

echo ""

if [ $VERBOSE -eq 1 ]; then
    echo "=============================================="
    echo "SQL COMMANDS THAT WILL BE USED:"
    echo "=============================================="
    echo ""
    echo "Execution method:"
    echo "  /QOpenSys/usr/bin/qsh -c \"db2 \\\"<sql>\\\"\""
    echo ""
    echo "SQL Table:"
    echo "  QSYS2.PTF_INFO"
    echo ""
    echo "Columns available:"
    echo "  PTF_IDENTIFIER, PTF_PRODUCT_ID, PTF_LOADED_STATUS,"
    echo "  PTF_IPL_ACTION, PTF_ACTION_PENDING, PTF_IPL_REQUIRED,"
    echo "  PTF_CREATION_TIMESTAMP, PTF_STATUS_TIMESTAMP,"
    echo "  PTF_SAVE_FILE, PTF_SUPERSEDED_BY_PTF, etc."
    echo ""
    echo "=============================================="
    echo ""
fi

# Run query and save to file
log_info "Querying PTF information..."

{
    echo "=============================================="
    echo "IBM i PTF List"
    echo "=============================================="
    echo ""
    echo "Generated: $(date)"
    echo "System:    $(hostname 2>/dev/null || echo 'unknown')"
    if [ -n "$PRODUCTS" ]; then
        echo "Products:  $PRODUCTS"
    else
        echo "Products:  ALL"
    fi
    echo ""
    echo "----------------------------------------------"
    echo ""

    if [ $SUMMARY -eq 1 ]; then
        echo "=== PTF Summary by Product and Status ==="
        echo ""
        list_ptfs_summary
    elif [ $ALL_COLUMNS -eq 1 ]; then
        echo "=== PTF Details (All Columns) ==="
        echo ""
        list_ptfs_all_columns
    else
        echo "=== PTF List ==="
        echo ""
        list_ptfs
    fi

    echo ""
    echo "----------------------------------------------"
    echo "End of report"

} > "$OUTPUT_FILE" 2>&1

_rc=$?

if [ $_rc -eq 0 ]; then
    log_info "Output written to: $OUTPUT_FILE"

    # Show line count
    _lines=$(wc -l < "$OUTPUT_FILE" | tr -d ' ')
    log_info "Total lines: $_lines"

    echo ""
    echo "=== First 30 lines of output ==="
    echo ""
    head -30 "$OUTPUT_FILE"
    echo ""
    echo "..."
    echo "(see $OUTPUT_FILE for complete list)"
else
    log_error "Query failed (rc=$_rc)"
    exit 1
fi

if [ $VERBOSE -eq 1 ]; then
    echo ""
    echo "=============================================="
    echo "VERBOSE SUMMARY"
    echo "=============================================="
    echo ""
    echo "Output file: $OUTPUT_FILE"
    echo "Query type:  $([ $SUMMARY -eq 1 ] && echo 'Summary' || ([ $ALL_COLUMNS -eq 1 ] && echo 'All columns' || echo 'Standard'))"
    echo ""
    echo "SQL execution method:"
    echo "  /QOpenSys/usr/bin/qsh -c \"db2 \\\"<sql>\\\"\""
    echo ""
fi
