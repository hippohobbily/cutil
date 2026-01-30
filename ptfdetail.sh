#!/QOpenSys/usr/bin/sh
#
# ptfdetail.sh - Get detailed PTF information including objects
#
# Uses DSPPTF with various detail options to extract PTF information.
# Simpler than listptfobj.sh - focuses on one PTF at a time.
#
# Usage:
#   ./ptfdetail.sh SI12345                    # Single PTF (auto-detect product)
#   ./ptfdetail.sh SI12345 5770SS1            # PTF with explicit product
#   ./ptfdetail.sh -c SI12345                 # Show cover letter
#   ./ptfdetail.sh -o SI12345                 # Show objects (DETAIL(*OBJ))
#   ./ptfdetail.sh -a SI12345                 # All details
#
# Note: DETAIL(*OBJ) may not be available for all PTFs
#

#----------------------------------------------------------------------
# Configuration
#----------------------------------------------------------------------

TEMP_DIR="/tmp/ptfdetail_$$"
SHOW_COVER=0
SHOW_OBJECTS=0
SHOW_ALL=0

#----------------------------------------------------------------------
# Functions
#----------------------------------------------------------------------

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

usage() {
    cat << 'EOF'
ptfdetail.sh - Get detailed PTF information

USAGE:
    ./ptfdetail.sh [options] <ptf_id> [product_id]

OPTIONS:
    -c          Show cover letter
    -o          Show affected objects
    -a          Show all details (cover + objects)
    -h          Show this help

ARGUMENTS:
    ptf_id      PTF identifier (e.g., SI12345)
    product_id  Product ID (optional, auto-detected if not specified)

EXAMPLES:
    ./ptfdetail.sh SI12345              # Basic info
    ./ptfdetail.sh -c SI12345           # With cover letter
    ./ptfdetail.sh -o SI12345 5770SS1   # Objects for OS PTF
    ./ptfdetail.sh -a SI12345           # Everything

EOF
}

run_sql() {
    /QOpenSys/usr/bin/qsh -c "db2 \"$1\"" 2>/dev/null
}

#----------------------------------------------------------------------
# Get product ID for a PTF
#----------------------------------------------------------------------

get_product_for_ptf() {
    _ptf="$1"
    run_sql "SELECT PTF_PRODUCT_ID FROM QSYS2.PTF_INFO WHERE PTF_IDENTIFIER = '$_ptf' FETCH FIRST 1 ROW ONLY" 2>/dev/null | \
        grep -E '[0-9]{4}[A-Z]{2}[0-9]' | awk '{print $1}' | head -1
}

#----------------------------------------------------------------------
# Get basic PTF info from SQL
#----------------------------------------------------------------------

show_ptf_info() {
    _ptf="$1"
    _product="$2"

    echo "=== PTF Information ==="
    echo ""

    run_sql "SELECT
        PTF_IDENTIFIER AS \"PTF ID\",
        PTF_PRODUCT_ID AS \"Product\",
        PTF_LOADED_STATUS AS \"Status\",
        PTF_IPL_ACTION AS \"IPL Action\",
        PTF_ACTION_PENDING AS \"Pending\",
        PTF_IPL_REQUIRED AS \"IPL Required\",
        PTF_CREATION_TIMESTAMP AS \"Created\",
        PTF_STATUS_TIMESTAMP AS \"Status Date\",
        PTF_SUPERSEDED_BY_PTF AS \"Superseded By\",
        PTF_MINIMUM_LEVEL AS \"Min Level\",
        PTF_MAXIMUM_LEVEL AS \"Max Level\",
        PTF_RELEASE_LEVEL AS \"Release\",
        PTF_SAVE_FILE AS \"Save File\"
    FROM QSYS2.PTF_INFO
    WHERE PTF_IDENTIFIER = '$_ptf'" | head -50
}

#----------------------------------------------------------------------
# Show PTF prerequisites
#----------------------------------------------------------------------

show_ptf_prereqs() {
    _ptf="$1"

    echo ""
    echo "=== Prerequisites ==="
    echo ""

    run_sql "SELECT
        REQUISITE_PTF_ID AS \"Required PTF\",
        REQUISITE_PRODUCT_ID AS \"Product\",
        REQUISITE_TYPE AS \"Type\",
        REQUISITE_IS_CONDITIONAL AS \"Conditional\",
        REQUISITE_LOADED_STATUS AS \"Status\"
    FROM QSYS2.PTF_REQUISITE
    WHERE PTF_IDENTIFIER = '$_ptf'
    ORDER BY REQUISITE_TYPE, REQUISITE_PTF_ID" 2>/dev/null | head -100

    _count=$(run_sql "SELECT COUNT(*) FROM QSYS2.PTF_REQUISITE WHERE PTF_IDENTIFIER = '$_ptf'" 2>/dev/null | grep -E '^\s*[0-9]+' | awk '{print $1}')
    if [ -z "$_count" ] || [ "$_count" = "0" ]; then
        echo "(No prerequisites found)"
    fi
}

#----------------------------------------------------------------------
# Show cover letter
#----------------------------------------------------------------------

show_cover_letter() {
    _ptf="$1"
    _product="$2"
    _outfile="$TEMP_DIR/cover.txt"

    echo ""
    echo "=== Cover Letter ==="
    echo ""

    # Generate cover letter to spool file
    system -q "DSPPTF LICPGM($_product) PTF($_ptf) COVER(*YES) OUTPUT(*PRINT)" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo "(Cover letter not available)"
        return 1
    fi

    # Copy spool to IFS
    system -q "CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)" 2>/dev/null

    if [ $? -ne 0 ]; then
        system -q "CPYSPLF FILE(QSYSPRT) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)" 2>/dev/null
    fi

    if [ -f "$_outfile" ]; then
        cat "$_outfile"
    else
        echo "(Could not retrieve cover letter)"
    fi
}

#----------------------------------------------------------------------
# Show affected objects via DSPPTF
#----------------------------------------------------------------------

show_objects() {
    _ptf="$1"
    _product="$2"
    _outfile="$TEMP_DIR/objects.txt"

    echo ""
    echo "=== Affected Objects ==="
    echo ""

    # DSPPTF with DETAIL(*OBJ) shows objects
    # But this is interactive, need to capture via OUTPUT(*PRINT)
    # Note: Not all PTFs support DETAIL(*OBJ)

    # Method 1: Try to get object list from DSPPTF output
    system -q "DSPPTF LICPGM($_product) PTF($_ptf) OUTPUT(*PRINT)" 2>/dev/null

    if [ $? -eq 0 ]; then
        system -q "CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)" 2>/dev/null

        if [ -f "$_outfile" ]; then
            # Parse for object information
            echo "From DSPPTF output:"
            echo ""

            # Look for object-related lines
            grep -E 'Object|object|OBJECT|Library|library|LIBRARY|Program|program|PROGRAM|\*PGM|\*SRVPGM|\*MODULE|\*FILE|\*CMD' "$_outfile" | head -50

            # Also show object count if mentioned
            grep -iE 'object.*count|number.*object' "$_outfile"
        fi
    fi

    # Method 2: Parse cover letter for objects (more reliable)
    echo ""
    echo "From cover letter analysis:"
    echo ""

    _coverfile="$TEMP_DIR/cover_obj.txt"
    system -q "DSPPTF LICPGM($_product) PTF($_ptf) COVER(*YES) OUTPUT(*PRINT)" 2>/dev/null
    system -q "CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_coverfile') SPLNBR(*LAST)" 2>/dev/null

    if [ -f "$_coverfile" ]; then
        # Look for common object patterns
        echo "Libraries and objects mentioned:"
        grep -E 'QSYS|QGPL|QUSRSYS|Q[A-Z]{2,8}' "$_coverfile" | \
            grep -v '^[[:space:]]*$' | \
            grep -vE 'LICPGM|PTF|Cover|cover|COVER' | \
            sort -u | head -30

        echo ""
        echo "Object types mentioned:"
        grep -oE '\*[A-Z]{2,10}' "$_coverfile" | sort | uniq -c | sort -rn | head -20
    else
        echo "(Object details not available)"
    fi
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Parse options
while getopts "coah" opt; do
    case $opt in
        c)
            SHOW_COVER=1
            ;;
        o)
            SHOW_OBJECTS=1
            ;;
        a)
            SHOW_ALL=1
            SHOW_COVER=1
            SHOW_OBJECTS=1
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

# Get PTF ID
PTF_ID="$1"
PRODUCT="$2"

if [ -z "$PTF_ID" ]; then
    log_error "PTF ID required"
    usage
    exit 1
fi

# Uppercase PTF ID
PTF_ID=$(echo "$PTF_ID" | tr '[:lower:]' '[:upper:]')

# Auto-detect product if not specified
if [ -z "$PRODUCT" ]; then
    log_info "Detecting product for PTF $PTF_ID..."
    PRODUCT=$(get_product_for_ptf "$PTF_ID")

    if [ -z "$PRODUCT" ]; then
        log_error "Could not determine product for PTF $PTF_ID"
        log_error "Please specify product ID as second argument"
        exit 1
    fi
    log_info "Product: $PRODUCT"
fi

# Create temp directory
mkdir -p "$TEMP_DIR"
trap "rm -rf '$TEMP_DIR'" EXIT

# Header
echo "=============================================="
echo "PTF Detail: $PTF_ID"
echo "Product:    $PRODUCT"
echo "Date:       $(date)"
echo "=============================================="
echo ""

# Show basic info
show_ptf_info "$PTF_ID" "$PRODUCT"

# Show prerequisites
show_ptf_prereqs "$PTF_ID"

# Show cover letter if requested
if [ $SHOW_COVER -eq 1 ]; then
    show_cover_letter "$PTF_ID" "$PRODUCT"
fi

# Show objects if requested
if [ $SHOW_OBJECTS -eq 1 ]; then
    show_objects "$PTF_ID" "$PRODUCT"
fi

echo ""
echo "=============================================="
echo "End of PTF Detail"
echo "=============================================="
