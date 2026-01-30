#!/QOpenSys/usr/bin/sh
#
# listptfobj.sh - List objects touched by PTFs on IBM i
#
# For each PTF, extracts the cover letter and parses it to find
# affected objects (replaced, new, changed).
#
# Usage:
#   ./listptfobj.sh                     # All applied PTFs (SLOW!)
#   ./listptfobj.sh 5770SS1             # PTFs for specific product
#   ./listptfobj.sh -p SI12345          # Specific PTF
#   ./listptfobj.sh -p SI12345,SI12346  # Multiple specific PTFs
#   ./listptfobj.sh -l 10               # Limit to first 10 PTFs
#   ./listptfobj.sh -v                  # Verbose (show CL commands)
#   ./listptfobj.sh -h                  # Help
#
# Output: ~/ptf_objects.txt (default)
#
# Note: This can be SLOW for many PTFs as it processes each one individually.
#

#----------------------------------------------------------------------
# Configuration
#----------------------------------------------------------------------

OUTPUT_FILE="${HOME:-/QOpenSys/home/$USER}/ptf_objects.txt"
TEMP_DIR="/tmp/listptfobj_$$"
PRODUCT=""
SPECIFIC_PTFS=""
LIMIT=0
VERBOSE=0

#----------------------------------------------------------------------
# Logging
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

log_progress() {
    if [ $VERBOSE -eq 0 ]; then
        printf "\r[PROGRESS] %s" "$1"
    fi
}

#----------------------------------------------------------------------
# Usage
#----------------------------------------------------------------------

usage() {
    cat << 'EOF'
listptfobj.sh - List objects touched by PTFs

USAGE:
    ./listptfobj.sh [options] [product_id]

OPTIONS:
    -o FILE      Output file (default: ~/ptf_objects.txt)
    -p PTF_LIST  Specific PTF(s) to analyze (comma-separated)
    -l LIMIT     Limit number of PTFs to process
    -s STATUS    Filter by status (APPLIED, PERMANENT, ALL)
                 Default: APPLIED (includes both temp and perm applied)
    -v           Verbose output (show exact CL commands and SQL)
    -h           Show this help

ARGUMENTS:
    product_id   Product ID to filter (e.g., 5770SS1)

EXAMPLES:
    ./listptfobj.sh 5770SS1              # All applied PTFs for OS
    ./listptfobj.sh -p SI12345           # Single PTF details
    ./listptfobj.sh -p SI12345,SI67890   # Multiple specific PTFs
    ./listptfobj.sh -l 20 5770SS1        # First 20 PTFs for OS
    ./listptfobj.sh -s ALL 5770DG1       # All PTFs (any status) for HTTP
    ./listptfobj.sh -v -p SI12345        # Verbose mode showing all commands

OUTPUT FORMAT:
    PTF_ID,PRODUCT,OBJECT_LIB,OBJECT_NAME,OBJECT_TYPE,ACTION
    SI12345,5770SS1,QSYS,QZDASOINIT,*PGM,REPLACED
    SI12345,5770SS1,QSYS,NEWSRVPGM,*SRVPGM,NEW

NOTE:
    Processing many PTFs is SLOW (several seconds per PTF).
    Use -l to limit or specify specific PTFs with -p.

CL COMMANDS USED:
    DSPPTF LICPGM(product) PTF(ptfid) COVER(*YES) OUTPUT(*PRINT)
    CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('path') SPLNBR(*LAST)

SQL TABLES:
    QSYS2.PTF_INFO - PTF metadata
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
            log_debug "SQL result (first 5 lines):"
            echo "$_result" | head -5 | while read _line; do
                log_debug "  $_line"
            done
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
# Get list of PTFs to process
#----------------------------------------------------------------------

get_ptf_list() {
    _product="$1"
    _status="$2"
    _limit="$3"

    log_info "Building PTF query..."

    # Build WHERE clause
    _where=""

    # Product filter
    if [ -n "$_product" ]; then
        _where="WHERE PTF_PRODUCT_ID = '$_product'"
        log_debug "Adding product filter: $_product"
    fi

    # Status filter
    case "$_status" in
        APPLIED)
            if [ -n "$_where" ]; then
                _where="$_where AND PTF_LOADED_STATUS LIKE '%APPLIED%'"
            else
                _where="WHERE PTF_LOADED_STATUS LIKE '%APPLIED%'"
            fi
            log_debug "Adding status filter: APPLIED (temp or perm)"
            ;;
        PERMANENT)
            if [ -n "$_where" ]; then
                _where="$_where AND PTF_LOADED_STATUS = 'PERMANENTLY APPLIED'"
            else
                _where="WHERE PTF_LOADED_STATUS = 'PERMANENTLY APPLIED'"
            fi
            log_debug "Adding status filter: PERMANENTLY APPLIED only"
            ;;
        ALL)
            log_debug "No status filter (ALL statuses)"
            ;;
    esac

    # Build query
    _sql="SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID FROM QSYS2.PTF_INFO $_where ORDER BY PTF_PRODUCT_ID, PTF_IDENTIFIER"

    if [ "$_limit" -gt 0 ] 2>/dev/null; then
        _sql="$_sql FETCH FIRST $_limit ROWS ONLY"
        log_debug "Limiting to $_limit rows"
    fi

    log_info "Executing PTF list query..."

    # Run query and extract PTF_ID,PRODUCT pairs
    run_sql "$_sql" | grep -E '^\s*[A-Z]{2}[0-9]{5}' | while read _line; do
        # Parse line: "SI12345   5770SS1"
        _ptf=$(echo "$_line" | awk '{print $1}')
        _prod=$(echo "$_line" | awk '{print $2}')
        if [ -n "$_ptf" ] && [ -n "$_prod" ]; then
            log_debug "Found PTF: $_ptf ($_prod)"
            echo "$_ptf,$_prod"
        fi
    done
}

#----------------------------------------------------------------------
# Extract cover letter for a PTF
#----------------------------------------------------------------------

extract_cover_letter() {
    _ptf="$1"
    _product="$2"
    _outfile="$3"

    log_debug "Extracting cover letter for PTF $_ptf (product $_product)"

    # Step 1: Generate cover letter to spool file
    _cl_dspptf="DSPPTF LICPGM($_product) PTF($_ptf) COVER(*YES) OUTPUT(*PRINT)"
    log_info "  Step 1: Generate cover letter spool file"

    run_cl "$_cl_dspptf" "quiet"
    _rc1=$?

    if [ $_rc1 -ne 0 ]; then
        log_warn "  DSPPTF failed with rc=$_rc1"
        log_debug "  PTF may not exist or cover letter not available"
        return 1
    fi
    log_debug "  DSPPTF succeeded (spool file created)"

    # Step 2: Copy spool file to IFS stream file
    _cl_cpysplf="CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)"
    log_info "  Step 2: Copy spool to IFS: $_outfile"

    run_cl "$_cl_cpysplf" "quiet"
    _rc2=$?

    if [ $_rc2 -ne 0 ]; then
        log_debug "  CPYSPLF QPDSPPTF failed (rc=$_rc2), trying QSYSPRT..."

        # Try alternative spool file name
        _cl_cpysplf_alt="CPYSPLF FILE(QSYSPRT) TOFILE(*TOSTMF) TOSTMF('$_outfile') SPLNBR(*LAST)"
        log_cmd "system -q \"$_cl_cpysplf_alt\""

        run_cl "$_cl_cpysplf_alt" "quiet"
        _rc2=$?

        if [ $_rc2 -ne 0 ]; then
            log_warn "  CPYSPLF failed with rc=$_rc2"
            return 1
        fi
    fi

    # Verify file was created
    if [ -f "$_outfile" ]; then
        _filesize=$(wc -c < "$_outfile" | tr -d ' ')
        log_debug "  Cover letter extracted: $_filesize bytes"
        return 0
    else
        log_warn "  Output file not created"
        return 1
    fi
}

#----------------------------------------------------------------------
# Parse cover letter for objects
# Cover letters have various formats, we look for common patterns
#----------------------------------------------------------------------

parse_cover_letter() {
    _ptf="$1"
    _product="$2"
    _coverfile="$3"

    if [ ! -f "$_coverfile" ]; then
        log_debug "  Cover file not found: $_coverfile"
        return 1
    fi

    _filesize=$(wc -c < "$_coverfile" | tr -d ' ')
    _linecount=$(wc -l < "$_coverfile" | tr -d ' ')
    log_debug "  Parsing cover letter: $_linecount lines, $_filesize bytes"

    # Patterns to look for in cover letters:
    # - "Library    Object     Type"
    # - "QSYS       PROGRAM    *PGM"
    # - Object lists often appear after "Replaced Objects:" or similar headers

    _in_object_section=0
    _current_action="REPLACED"
    _objects_found=0

    while IFS= read -r _line; do
        # Detect section headers
        case "$_line" in
            *"Replaced Objects"*|*"REPLACED OBJECTS"*|*"Objects replaced"*)
                _in_object_section=1
                _current_action="REPLACED"
                log_debug "    Found section: Replaced Objects"
                continue
                ;;
            *"New Objects"*|*"NEW OBJECTS"*|*"Objects added"*)
                _in_object_section=1
                _current_action="NEW"
                log_debug "    Found section: New Objects"
                continue
                ;;
            *"Changed Objects"*|*"CHANGED OBJECTS"*|*"Objects changed"*)
                _in_object_section=1
                _current_action="CHANGED"
                log_debug "    Found section: Changed Objects"
                continue
                ;;
            *"Prerequisite"*|*"PREREQUISITE"*|*"Supersedes"*|*"SUPERSEDES"*)
                _in_object_section=0
                continue
                ;;
            *"----------------"*)
                # Separator line, might end a section
                continue
                ;;
        esac

        # Skip empty lines
        _trimmed=$(echo "$_line" | tr -d ' ')
        if [ -z "$_trimmed" ]; then
            continue
        fi

        # Look for object patterns: LIB/OBJ TYPE or LIB  OBJ  *TYPE
        # Pattern 1: QSYS/PROGRAM *PGM
        if echo "$_line" | grep -qE '^[[:space:]]*[A-Z][A-Z0-9_]{0,9}/[A-Z][A-Z0-9_]{0,9}[[:space:]]+\*[A-Z]+'; then
            _lib=$(echo "$_line" | sed -E 's|^[[:space:]]*([A-Z][A-Z0-9_]{0,9})/.*|\1|')
            _obj=$(echo "$_line" | sed -E 's|^[[:space:]]*[A-Z][A-Z0-9_]{0,9}/([A-Z][A-Z0-9_]{0,9})[[:space:]].*|\1|')
            _type=$(echo "$_line" | grep -oE '\*[A-Z]+' | head -1)
            if [ -n "$_lib" ] && [ -n "$_obj" ] && [ -n "$_type" ]; then
                log_debug "    Object found (pattern 1): $_lib/$_obj $_type"
                echo "$_ptf,$_product,$_lib,$_obj,$_type,$_current_action"
                _objects_found=$((_objects_found + 1))
            fi
            continue
        fi

        # Pattern 2: QSYS     PROGRAMNAME   *PGM (space-separated columns)
        if echo "$_line" | grep -qE '^[[:space:]]*[A-Z][A-Z0-9_#@$]{0,9}[[:space:]]+[A-Z][A-Z0-9_#@$]{0,9}[[:space:]]+\*[A-Z]+'; then
            _lib=$(echo "$_line" | awk '{print $1}')
            _obj=$(echo "$_line" | awk '{print $2}')
            _type=$(echo "$_line" | awk '{print $3}')
            # Validate
            if echo "$_lib" | grep -qE '^[A-Z][A-Z0-9_#@$]{0,9}$' && \
               echo "$_obj" | grep -qE '^[A-Z][A-Z0-9_#@$]{0,9}$' && \
               echo "$_type" | grep -qE '^\*[A-Z]+$'; then
                log_debug "    Object found (pattern 2): $_lib $_obj $_type"
                echo "$_ptf,$_product,$_lib,$_obj,$_type,$_current_action"
                _objects_found=$((_objects_found + 1))
            fi
            continue
        fi

        # Pattern 3: Just library and object on continuation (contextual)
        if [ $_in_object_section -eq 1 ]; then
            if echo "$_line" | grep -qE '^[[:space:]]*[A-Z][A-Z0-9_#@$]{0,9}[[:space:]]+[A-Z][A-Z0-9_#@$]{0,9}[[:space:]]*$'; then
                _lib=$(echo "$_line" | awk '{print $1}')
                _obj=$(echo "$_line" | awk '{print $2}')
                if [ -n "$_lib" ] && [ -n "$_obj" ]; then
                    log_debug "    Object found (pattern 3): $_lib $_obj *UNKNOWN"
                    echo "$_ptf,$_product,$_lib,$_obj,*UNKNOWN,$_current_action"
                    _objects_found=$((_objects_found + 1))
                fi
            fi
        fi

    done < "$_coverfile"

    log_debug "  Total objects found in cover letter: $_objects_found"
}

#----------------------------------------------------------------------
# Alternative: Use DSPPTF OUTPUT(*OUTFILE) for basic info
# Then query the outfile for object counts
#----------------------------------------------------------------------

get_ptf_object_count() {
    _ptf="$1"
    _product="$2"

    # DSPPTF can show object count but not details in outfile
    # This is a fallback if cover letter parsing fails

    _sql="SELECT PTF_IDENTIFIER, COALESCE(PTF_OBJECT_COUNT, 0) as OBJ_COUNT
          FROM QSYS2.PTF_INFO
          WHERE PTF_IDENTIFIER = '$_ptf' AND PTF_PRODUCT_ID = '$_product'"

    run_sql "$_sql" | grep -E '^\s*[A-Z]{2}[0-9]{5}' | awk '{print $2}'
}

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Parse options
STATUS_FILTER="APPLIED"

while getopts "o:p:l:s:vh" opt; do
    case $opt in
        o)
            OUTPUT_FILE="$OPTARG"
            ;;
        p)
            SPECIFIC_PTFS="$OPTARG"
            ;;
        l)
            LIMIT="$OPTARG"
            ;;
        s)
            STATUS_FILTER=$(echo "$OPTARG" | tr '[:lower:]' '[:upper:]')
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

# Product ID from remaining args
PRODUCT="$1"

# Create temp directory
mkdir -p "$TEMP_DIR"
trap "rm -rf '$TEMP_DIR'" EXIT

# Header
echo "=============================================="
echo "IBM i PTF Object Analysis"
echo "=============================================="
echo ""
echo "Date:       $(date)"
echo "Output:     $OUTPUT_FILE"
echo "Temp Dir:   $TEMP_DIR"
echo "Verbose:    $([ $VERBOSE -eq 1 ] && echo 'YES' || echo 'NO')"

if [ -n "$SPECIFIC_PTFS" ]; then
    echo "PTFs:       $SPECIFIC_PTFS"
elif [ -n "$PRODUCT" ]; then
    echo "Product:    $PRODUCT"
else
    echo "Product:    ALL"
fi

echo "Status:     $STATUS_FILTER"
if [ "$LIMIT" -gt 0 ] 2>/dev/null; then
    echo "Limit:      $LIMIT PTFs"
fi
echo ""

if [ $VERBOSE -eq 1 ]; then
    echo "=============================================="
    echo "CL COMMANDS THAT WILL BE USED:"
    echo "=============================================="
    echo ""
    echo "1. Query PTF list:"
    echo "   SQL: SELECT PTF_IDENTIFIER, PTF_PRODUCT_ID FROM QSYS2.PTF_INFO ..."
    echo "   Via: /QOpenSys/usr/bin/qsh -c \"db2 \\\"<sql>\\\"\""
    echo ""
    echo "2. For each PTF, extract cover letter:"
    echo "   CL:  DSPPTF LICPGM(<product>) PTF(<ptfid>) COVER(*YES) OUTPUT(*PRINT)"
    echo "   Via: system -q \"<cl_command>\""
    echo ""
    echo "3. Copy spool file to IFS:"
    echo "   CL:  CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('<path>') SPLNBR(*LAST)"
    echo "   Via: system -q \"<cl_command>\""
    echo ""
    echo "=============================================="
    echo ""
fi

# Build PTF list
log_info "Building PTF list..."

PTF_LIST_FILE="$TEMP_DIR/ptf_list.txt"

if [ -n "$SPECIFIC_PTFS" ]; then
    log_info "Using specific PTFs: $SPECIFIC_PTFS"

    # Use specific PTFs provided
    echo "$SPECIFIC_PTFS" | tr ',' '\n' | while read _ptf; do
        _ptf=$(echo "$_ptf" | tr -d ' ')
        if [ -n "$_ptf" ]; then
            log_debug "Looking up product for PTF: $_ptf"

            # Get product for this PTF
            _lookup_sql="SELECT PTF_PRODUCT_ID FROM QSYS2.PTF_INFO WHERE PTF_IDENTIFIER = '$_ptf' FETCH FIRST 1 ROW ONLY"
            _prod=$(run_sql "$_lookup_sql" | grep -E '^\s*[0-9]{4}[A-Z]{2}[0-9]|^\s*5[0-9]{3}[A-Z]{2}[0-9]' | awk '{print $1}')

            if [ -n "$_prod" ]; then
                log_debug "  Found product: $_prod"
                echo "$_ptf,$_prod"
            else
                log_warn "  Could not find product for $_ptf, defaulting to 5770SS1"
                echo "$_ptf,5770SS1"
            fi
        fi
    done > "$PTF_LIST_FILE"
else
    # Get PTF list from SQL
    get_ptf_list "$PRODUCT" "$STATUS_FILTER" "$LIMIT" > "$PTF_LIST_FILE"
fi

PTF_COUNT=$(wc -l < "$PTF_LIST_FILE" | tr -d ' ')
log_info "Found $PTF_COUNT PTFs to process"

if [ $VERBOSE -eq 1 ]; then
    log_debug "PTF list file: $PTF_LIST_FILE"
    log_debug "Contents:"
    cat "$PTF_LIST_FILE" | while read _line; do
        log_debug "  $_line"
    done
fi

if [ "$PTF_COUNT" -eq 0 ]; then
    log_warn "No PTFs found matching criteria"
    log_info "Check that:"
    log_info "  1. Product ID is valid (e.g., 5770SS1)"
    log_info "  2. PTFs exist with status: $STATUS_FILTER"
    log_info "  3. You have authority to query QSYS2.PTF_INFO"
    exit 0
fi

if [ "$PTF_COUNT" -gt 50 ] && [ -z "$SPECIFIC_PTFS" ]; then
    log_warn "Processing $PTF_COUNT PTFs will take a while..."
    log_warn "Consider using -l to limit or -p for specific PTFs"
fi

echo ""

# Initialize output file
{
    echo "=============================================="
    echo "IBM i PTF Object Analysis"
    echo "=============================================="
    echo ""
    echo "Generated: $(date)"
    if [ -n "$PRODUCT" ]; then
        echo "Product:   $PRODUCT"
    fi
    echo "PTF Count: $PTF_COUNT"
    echo ""
    echo "=============================================="
    echo ""
    echo "PTF_ID,PRODUCT,LIBRARY,OBJECT,TYPE,ACTION"
} > "$OUTPUT_FILE"

# Process each PTF
PROCESSED=0
OBJECTS_FOUND=0
COVER_FILE="$TEMP_DIR/cover.txt"

log_info "Starting PTF processing..."
echo ""

while IFS=',' read -r PTF_ID PTF_PRODUCT; do
    PROCESSED=$((PROCESSED + 1))

    if [ $VERBOSE -eq 1 ]; then
        echo ""
        echo "========================================"
        log_info "[$PROCESSED/$PTF_COUNT] Processing PTF: $PTF_ID"
        log_info "  Product: $PTF_PRODUCT"
        echo "========================================"
    else
        log_progress "[$PROCESSED/$PTF_COUNT] $PTF_ID              "
    fi

    # Remove old cover file
    rm -f "$COVER_FILE"

    # Extract cover letter
    extract_cover_letter "$PTF_ID" "$PTF_PRODUCT" "$COVER_FILE"
    _extract_rc=$?

    if [ $_extract_rc -eq 0 ] && [ -f "$COVER_FILE" ]; then
        # Parse cover letter for objects
        log_info "  Step 3: Parse cover letter for objects"

        _obj_count=0
        parse_cover_letter "$PTF_ID" "$PTF_PRODUCT" "$COVER_FILE" | while read _obj_line; do
            echo "$_obj_line" >> "$OUTPUT_FILE"
            _obj_count=$((_obj_count + 1))
        done

        # Count objects found (re-parse for count)
        _found=$(parse_cover_letter "$PTF_ID" "$PTF_PRODUCT" "$COVER_FILE" | wc -l)
        OBJECTS_FOUND=$((OBJECTS_FOUND + _found))

        if [ $VERBOSE -eq 1 ]; then
            log_info "  Result: Found $_found objects"
        fi
    else
        if [ $VERBOSE -eq 1 ]; then
            log_warn "  Could not extract cover letter for $PTF_ID"
            log_debug "  This may be normal - not all PTFs have cover letters"
        fi
        # Record PTF with no objects found
        echo "$PTF_ID,$PTF_PRODUCT,N/A,NO_COVER_LETTER,*NONE,UNKNOWN" >> "$OUTPUT_FILE"
    fi

done < "$PTF_LIST_FILE"

# Clear progress line
printf "\r                                                  \r"

# Summary
{
    echo ""
    echo "=============================================="
    echo "Summary"
    echo "=============================================="
    echo "PTFs processed:  $PROCESSED"
    echo "Objects found:   $OBJECTS_FOUND"
} >> "$OUTPUT_FILE"

echo ""
echo "=============================================="
log_info "Processing complete!"
echo "=============================================="
log_info "PTFs processed:  $PROCESSED"
log_info "Output written:  $OUTPUT_FILE"
echo ""

# Show sample output
echo "=== Sample Output (first 30 lines) ==="
echo ""
head -30 "$OUTPUT_FILE"
echo ""
echo "..."
echo "(see $OUTPUT_FILE for complete list)"

# Show object count by library
echo ""
echo "=== Objects by Library (top 10) ==="
grep -v '^#' "$OUTPUT_FILE" | grep -v '^PTF_ID' | grep -v '^===' | grep -v '^$' | \
    grep -v 'Generated:' | grep -v 'Product:' | grep -v 'PTF Count:' | grep -v 'Summary' | \
    grep -v 'processed:' | grep -v 'found:' | \
    cut -d',' -f3 | sort | uniq -c | sort -rn | head -10

if [ $VERBOSE -eq 1 ]; then
    echo ""
    echo "=============================================="
    echo "VERBOSE SUMMARY"
    echo "=============================================="
    echo ""
    echo "Temp directory used: $TEMP_DIR"
    echo "Cover letter file:   $COVER_FILE"
    echo "PTF list file:       $PTF_LIST_FILE"
    echo ""
    echo "Commands executed for each PTF:"
    echo "  1. DSPPTF LICPGM(<prod>) PTF(<ptf>) COVER(*YES) OUTPUT(*PRINT)"
    echo "  2. CPYSPLF FILE(QPDSPPTF) TOFILE(*TOSTMF) TOSTMF('<file>') SPLNBR(*LAST)"
    echo ""
fi
