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

#----------------------------------------------------------------------
# Functions
#----------------------------------------------------------------------

usage() {
    cat << 'EOF'
listptf.sh - List PTFs installed on IBM i

USAGE:
    ./listptf.sh [options] [product_id ...]

OPTIONS:
    -o FILE     Write output to FILE (default: ~/ptf_list.txt)
    -a          Show all columns (verbose)
    -s          Summary only (count by status)
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
    ./listptf.sh -a 5770SS1             # Verbose output for OS

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

EOF
}

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

#----------------------------------------------------------------------
# Run SQL query using QShell db2 utility
#----------------------------------------------------------------------

run_sql() {
    _sql="$1"
    /QOpenSys/usr/bin/qsh -c "db2 \"$_sql\"" 2>/dev/null
}

#----------------------------------------------------------------------
# Build WHERE clause for product filter
#----------------------------------------------------------------------

build_product_filter() {
    if [ -z "$PRODUCTS" ]; then
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

    run_sql "$_sql"
}

#----------------------------------------------------------------------
# List PTFs (verbose - all columns)
#----------------------------------------------------------------------

list_ptfs_verbose() {
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

    run_sql "$_sql"
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Parse options
VERBOSE=0
SUMMARY=0

while getopts "o:ash" opt; do
    case $opt in
        o)
            OUTPUT_FILE="$OPTARG"
            ;;
        a)
            VERBOSE=1
            ;;
        s)
            SUMMARY=1
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

if [ -n "$PRODUCTS" ]; then
    echo "Products:   $PRODUCTS"
else
    echo "Products:   ALL"
fi

echo ""

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
    elif [ $VERBOSE -eq 1 ]; then
        echo "=== PTF Details (Verbose) ==="
        echo ""
        list_ptfs_verbose
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
