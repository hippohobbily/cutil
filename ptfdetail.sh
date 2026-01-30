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
#   ./ptfdetail.sh -v SI12345                 # Verbose (show CL commands)
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
VERBOSE=0

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
ptfdetail.sh - Get detailed PTF information

USAGE:
    ./ptfdetail.sh [options] <ptf_id> [product_id]

OPTIONS:
    -c          Show cover letter
    -o          Show affected objects
    -a          Show all details (cover + objects)
    -v          Verbose mode (show exact CL commands and SQL)
    -h          Show this help

ARGUMENTS:
    ptf_id      PTF identifier (e.g., SI12345)
    product_id  Product ID (optional, auto-detected if not specified)

EXAMPLES:
    ./ptfdetail.sh SI12345              # Basic info
    ./ptfdetail.sh -c SI12345           # With cover letter
    ./ptfdetail.sh -o SI12345 5770SS1   # Objects for OS PTF
    ./ptfdetail.sh -a SI12345           # Everything
    ./ptfdetail.sh -v SI12345           # Verbose with CL command details

CL COMMANDS USED:
    DSPPTF LICPGM(product) PTF(ptfid) OUTPUT(*PRINT)
    DSPPTF LICPGM(product) PTF(ptfid) COVER(*YES) OUTPUT(*PRINT)
    CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('path') SPLNBR(*LAST)

SQL TABLES:
    QSYS2.PTF_INFO - PTF metadata and status
    QSYS2.PTF_REQUISITE - PTF prerequisites

EOF
}

#----------------------------------------------------------------------
# Run SQL query
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
# Run CL command via system utility
#----------------------------------------------------------------------

run_cl() {
    _cl_cmd="$1"
    _quiet="$2"

    log_cmd "system ${_quiet:+-q} \"$_cl_cmd\""

    if [ "$_quiet" = "quiet" ]; then
        system -q "$_cl_cmd" 2>&1
    else
        system "$_cl_cmd" 2>&1
    fi
    _rc=$?

    log_debug "CL return code: $_rc"
    return $_rc
}

#----------------------------------------------------------------------
# Get product ID for a PTF
#----------------------------------------------------------------------

get_product_for_ptf() {
    _ptf="$1"

    log_info "Looking up product for PTF $_ptf..."

    _sql="SELECT PTF_PRODUCT_ID FROM QSYS2.PTF_INFO WHERE PTF_IDENTIFIER = '$_ptf' FETCH FIRST 1 ROW ONLY"

    run_sql "$_sql" 2>/dev/null | \
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

    _sql="SELECT
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
    WHERE PTF_IDENTIFIER = '$_ptf'"

    log_info "Querying PTF metadata from QSYS2.PTF_INFO..."

    run_sql "$_sql" | head -50
}

#----------------------------------------------------------------------
# Show PTF prerequisites
#----------------------------------------------------------------------

show_ptf_prereqs() {
    _ptf="$1"

    echo ""
    echo "=== Prerequisites ==="
    echo ""

    _sql="SELECT
        REQUISITE_PTF_ID AS \"Required PTF\",
        REQUISITE_PRODUCT_ID AS \"Product\",
        REQUISITE_TYPE AS \"Type\",
        REQUISITE_IS_CONDITIONAL AS \"Conditional\",
        REQUISITE_LOADED_STATUS AS \"Status\"
    FROM QSYS2.PTF_REQUISITE
    WHERE PTF_IDENTIFIER = '$_ptf'
    ORDER BY REQUISITE_TYPE, REQUISITE_PTF_ID"

    log_info "Querying prerequisites from QSYS2.PTF_REQUISITE..."

    run_sql "$_sql" 2>/dev/null | head -100

    _count_sql="SELECT COUNT(*) FROM QSYS2.PTF_REQUISITE WHERE PTF_IDENTIFIER = '$_ptf'"
    _count=$(run_sql "$_count_sql" 2>/dev/null | grep -E '^\s*[0-9]+' | awk '{print $1}')

    if [ -z "$_count" ] || [ "$_count" = "0" ]; then
        echo "(No prerequisites found)"
    else
        log_debug "Found $_count prerequisites"
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

    # Step 1: Generate cover letter to spool file
    _cl_dspptf="DSPPTF LICPGM($_product) PTF($_ptf) COVER(*YES) OUTPUT(*PRINT)"

    log_info "Step 1: Generate cover letter spool file"
    log_info "  CL Command: $_cl_dspptf"

    run_cl "$_cl_dspptf" "quiet"
    _rc1=$?

    if [ $_rc1 -ne 0 ]; then
        log_warn "DSPPTF failed with rc=$_rc1"
        echo "(Cover letter not available)"
        return 1
    fi

    log_debug "DSPPTF succeeded (spool file created)"

    # Step 2: Copy spool to IFS
    _cl_cpysplf="CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)"

    log_info "Step 2: Copy spool file to IFS"
    log_info "  CL Command: $_cl_cpysplf"
    log_info "  Output file: $_outfile"

    run_cl "$_cl_cpysplf" "quiet"
    _rc2=$?

    if [ $_rc2 -ne 0 ]; then
        log_debug "CPYSPLF QPDSPPTF failed (rc=$_rc2), trying QSYSPRT..."

        _cl_cpysplf_alt="CPYSPLF FILE(QSYSPRT) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)"
        log_cmd "system -q \"$_cl_cpysplf_alt\""

        run_cl "$_cl_cpysplf_alt" "quiet"
        _rc2=$?
    fi

    if [ -f "$_outfile" ]; then
        _filesize=$(wc -c < "$_outfile" | tr -d ' ')
        _linecount=$(wc -l < "$_outfile" | tr -d ' ')
        log_info "Cover letter retrieved: $_linecount lines, $_filesize bytes"
        echo ""
        cat "$_outfile"
    else
        log_warn "Output file not created"
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
    _cl_dspptf="DSPPTF LICPGM($_product) PTF($_ptf) OUTPUT(*PRINT)"

    log_info "Method 1: Standard DSPPTF output"
    log_info "  CL Command: $_cl_dspptf"

    run_cl "$_cl_dspptf" "quiet"
    _rc=$?

    if [ $_rc -eq 0 ]; then
        _cl_cpysplf="CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)"
        log_info "  Copying spool: $_cl_cpysplf"

        run_cl "$_cl_cpysplf" "quiet"

        if [ -f "$_outfile" ]; then
            _filesize=$(wc -c < "$_outfile" | tr -d ' ')
            log_debug "DSPPTF output: $_filesize bytes"

            # Parse for object information
            echo "From DSPPTF output:"
            echo ""

            # Look for object-related lines
            _obj_lines=$(grep -E 'Object|object|OBJECT|Library|library|LIBRARY|Program|program|PROGRAM|\*PGM|\*SRVPGM|\*MODULE|\*FILE|\*CMD' "$_outfile" | head -50)

            if [ -n "$_obj_lines" ]; then
                echo "$_obj_lines"
            else
                echo "(No object references found in DSPPTF output)"
            fi

            # Also show object count if mentioned
            _count_line=$(grep -iE 'object.*count|number.*object' "$_outfile")
            if [ -n "$_count_line" ]; then
                echo ""
                echo "Object count: $_count_line"
            fi
        fi
    else
        log_warn "DSPPTF failed with rc=$_rc"
    fi

    # Method 2: Parse cover letter for objects (more reliable)
    echo ""
    echo "From cover letter analysis:"
    echo ""

    _coverfile="$TEMP_DIR/cover_obj.txt"
    _cl_dspptf_cover="DSPPTF LICPGM($_product) PTF($_ptf) COVER(*YES) OUTPUT(*PRINT)"

    log_info "Method 2: Cover letter analysis"
    log_info "  CL Command: $_cl_dspptf_cover"

    run_cl "$_cl_dspptf_cover" "quiet"
    _rc2=$?

    if [ $_rc2 -eq 0 ]; then
        _cl_cpysplf2="CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_coverfile') SPLNBR(*LAST)"
        log_info "  Copying spool: $_cl_cpysplf2"

        run_cl "$_cl_cpysplf2" "quiet"
    fi

    if [ -f "$_coverfile" ]; then
        _filesize=$(wc -c < "$_coverfile" | tr -d ' ')
        log_debug "Cover letter for object analysis: $_filesize bytes"

        # Look for common object patterns
        echo "Libraries and objects mentioned:"
        _lib_objs=$(grep -E 'QSYS|QGPL|QUSRSYS|Q[A-Z]{2,8}' "$_coverfile" | \
            grep -v '^[[:space:]]*$' | \
            grep -vE 'LICPGM|PTF|Cover|cover|COVER' | \
            sort -u | head -30)

        if [ -n "$_lib_objs" ]; then
            echo "$_lib_objs"
        else
            echo "(No library/object references found)"
        fi

        echo ""
        echo "Object types mentioned:"
        _types=$(grep -oE '\*[A-Z]{2,10}' "$_coverfile" | sort | uniq -c | sort -rn | head -20)

        if [ -n "$_types" ]; then
            echo "$_types"
        else
            echo "(No object types found)"
        fi
    else
        log_warn "Could not retrieve cover letter for object analysis"
        echo "(Object details not available)"
    fi
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Parse options
while getopts "coavh" opt; do
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
echo "Temp Dir:   $TEMP_DIR"
echo "Verbose:    $([ $VERBOSE -eq 1 ] && echo 'YES' || echo 'NO')"
echo "=============================================="
echo ""

if [ $VERBOSE -eq 1 ]; then
    echo "=============================================="
    echo "CL COMMANDS THAT WILL BE USED:"
    echo "=============================================="
    echo ""
    echo "1. Query PTF info:"
    echo "   SQL: SELECT ... FROM QSYS2.PTF_INFO WHERE PTF_IDENTIFIER = '$PTF_ID'"
    echo "   Via: /QOpenSys/usr/bin/qsh -c \"db2 \\\"<sql>\\\"\""
    echo ""
    echo "2. Query prerequisites:"
    echo "   SQL: SELECT ... FROM QSYS2.PTF_REQUISITE WHERE PTF_IDENTIFIER = '$PTF_ID'"
    echo "   Via: /QOpenSys/usr/bin/qsh -c \"db2 \\\"<sql>\\\"\""
    echo ""
    if [ $SHOW_COVER -eq 1 ] || [ $SHOW_OBJECTS -eq 1 ]; then
        echo "3. Display PTF (cover letter):"
        echo "   CL:  DSPPTF LICPGM($PRODUCT) PTF($PTF_ID) COVER(*YES) OUTPUT(*PRINT)"
        echo "   Via: system -q \"<cl_command>\""
        echo ""
        echo "4. Copy spool file to IFS:"
        echo "   CL:  CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('<path>') SPLNBR(*LAST)"
        echo "   Via: system -q \"<cl_command>\""
        echo ""
    fi
    echo "=============================================="
    echo ""
fi

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

if [ $VERBOSE -eq 1 ]; then
    echo ""
    echo "=============================================="
    echo "VERBOSE SUMMARY"
    echo "=============================================="
    echo ""
    echo "Temp directory used: $TEMP_DIR"
    echo "Files created:"
    ls -la "$TEMP_DIR" 2>/dev/null | grep -v "^total" | grep -v "^d"
    echo ""
    echo "CL Commands executed:"
    echo "  - DSPPTF for cover letter/objects"
    echo "  - CPYSPLF to copy spool files to IFS"
    echo ""
    echo "SQL Tables queried:"
    echo "  - QSYS2.PTF_INFO"
    echo "  - QSYS2.PTF_REQUISITE"
    echo ""
fi
