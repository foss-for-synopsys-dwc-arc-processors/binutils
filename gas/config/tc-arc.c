/* tc-arc.c -- Assembler for the ARC
   Copyright 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2005, 2006, 2007, 2008, 2009, 2011
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   Copyright 2007-2012 Synopsys Inc
   Contributor: Codito Technologies (Support for PIC)
   Contributor: Brendon Kehoe <brendan@zen.org>
   Contributor: Michael Eager <eager@eagercon.com>
   Contributor: Joern Rennecke <joern.rennecke@embecosm.com>
   Contributor: Claudiu Zissulescu <claziss@synopsys.com>

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"
#include "struc-symbol.h"
#include "safe-ctype.h"
#include "subsegs.h"
#include "../opcodes/arc-ext.h"
#include "elf/arc.h"
#include "dwarf2dbg.h"

/* #define DEBUG_INST_PATTERN 0 */
#define GAS_DEBUG_STMT(x)
#define GAS_DEBUG_PIC(x)
/* fprintf(stderr,"At %d in %s current_type is %s\n",
   __LINE__,__PRETTY_FUNCTION__,
   (current_pic_flag == GOT_TYPE)?"GOT":"NO_TYPE")
*/

extern int arc_get_mach (char *);
extern int arc_insn_not_jl (arc_insn);
extern int arc_test_wb(void);
extern unsigned char arc_get_branch_prediction(void);
extern void arc_reset_branch_prediction(void);
extern unsigned char em_jumplink_or_jump_insn (arc_insn insn,
					       int compact_insn16);
extern unsigned char em_branch_or_jump_insn (arc_insn insn,
					     int compact_insn16);

extern int arc_get_noshortcut_flag (void);
/* Commented out because it triggers error regarding invalid storage class,
   and it is not needed. */
/* static void arc_set_ext_seg (enum ExtOperType, int, int, int); */

extern int a4_brk_insn(arc_insn insn);
extern int ac_brk_s_insn(arc_insn insn);
extern int ARC700_rtie_insn(arc_insn insn);

static arc_insn arc_insert_operand (arc_insn, long *,
				    const struct arc_operand *, int,
				    const struct arc_operand_value *,
				    offsetT, char *, unsigned int);
static valueT md_chars_to_number (char *, int);

static void arc_common (int);
/* Commented out because it triggers error regarding invalid storage class,
   and it is not needed. */
/* static void arc_extoper (int); */
static void arc_option (int);
static int  arc_get_sda_reloc (arc_insn, int);

static void init_opcode_tables (int);
static void arc_ac_extinst (int);
static void arc_extra_reloc (int);

/* fields for extended instruction format in extmap section */
static int use_extended_instruction_format=0;
static unsigned char extended_register_format[RCLASS_SET_SIZE];
static unsigned char extended_operand_format1[OPD_FORMAT_SIZE];
static unsigned char extended_operand_format2[OPD_FORMAT_SIZE];
static unsigned char extended_operand_format3[OPD_FORMAT_SIZE];
static unsigned char extended_instruction_flags[4];

symbolS * GOT_symbol = 0;

static const struct suffix_classes
{
  char *name;
  int  len;
} suffixclass[] =
{
  { "SUFFIX_COND|SUFFIX_FLAG",23 },
  { "SUFFIX_FLAG", 11 },
  { "SUFFIX_COND", 11 },
  /* START ARC LOCAL */
  { "SUFFIX_DIRECT", 13 },
  /* END ARC LOCAL */
  { "SUFFIX_NONE", 11 }
};

#define MAXSUFFIXCLASS (sizeof (suffixclass) / sizeof (struct suffix_classes))

static const struct syntax_classes
{
  char *name;
  int  len;
  int  class;
} syntaxclass[] =
{
  { "SYNTAX_3OP|OP1_MUST_BE_IMM", 26, SYNTAX_3OP|OP1_MUST_BE_IMM|SYNTAX_VALID },
  { "OP1_MUST_BE_IMM|SYNTAX_3OP", 26, OP1_MUST_BE_IMM|SYNTAX_3OP|SYNTAX_VALID },
  { "SYNTAX_2OP|OP1_IMM_IMPLIED", 26, SYNTAX_2OP|OP1_IMM_IMPLIED|SYNTAX_VALID },
  { "OP1_IMM_IMPLIED|SYNTAX_2OP", 26, OP1_IMM_IMPLIED|SYNTAX_2OP|SYNTAX_VALID },
  { "SYNTAX_3OP",                 10, SYNTAX_3OP|SYNTAX_VALID },
  { "SYNTAX_2OP",                 10, SYNTAX_2OP|SYNTAX_VALID },
};

#define MAXSYNTAXCLASS (sizeof (syntaxclass) / sizeof (struct syntax_classes))


/* This array holds the chars that always start a comment.  If the
   pre-processor is disabled, these aren't very useful.  */
const char comment_chars[] = "#;";

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.  */
/* Also note that comments started like this one will always
   work if '/' isn't otherwise defined.  */
const char line_comment_chars[] = "#";

const char line_separator_chars[] = "`";

/* Chars that can be used to separate mant from exp in floating point nums.  */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant
   As in 0f12.456 or 0d1.2345e12.  */
const char FLT_CHARS[] = "rRsSfFdD";

/* Byte order.  */
extern int target_big_endian;
const char *arc_target_format = DEFAULT_TARGET_FORMAT;
static int byte_order = DEFAULT_BYTE_ORDER;

static segT arcext_section;

/* One of bfd_mach_arc_n.  */
static int arc_mach_type = bfd_mach_arc_a5;

/* Non-zero if the cpu type has been explicitly specified.  */
static int mach_type_specified_p = 0;

/* This is a flag that is set when an instruction is being assembled and
   otherwise it is reset.  */
static int assembling_instruction = 0;

/* Non-zero if opcode tables have been initialized.
   A .option command must appear before any instructions.  */
static int cpu_tables_init_p = 0;


/* Bit field of extension instruction options.  */
static unsigned long extinsnlib = 0;

#define SWAP_INSN		0x1
#define NORM_INSN		(SWAP_INSN << 1)
#define BARREL_SHIFT_INSN	(NORM_INSN << 1)
#define MIN_MAX_INSN		(BARREL_SHIFT_INSN << 1)
#define NO_MPY_INSN		(MIN_MAX_INSN << 1)
#define EA_INSN			(NO_MPY_INSN << 1)
#define MUL64_INSN		(EA_INSN << 1)
#define SIMD_INSN               (MUL64_INSN << 1)
#define SP_FLOAT_INSN           (SIMD_INSN << 1)
#define DP_FLOAT_INSN           (SP_FLOAT_INSN << 1)
#define XMAC_D16                (DP_FLOAT_INSN << 1)
#define XMAC_24                 (XMAC_D16      << 1)
#define DSP_PACKA               (XMAC_24       << 1)
#define CRC                     (DSP_PACKA     << 1)
#define DVBF                    (CRC           << 1)
#define TELEPHONY               (DVBF          << 1)
#define XYMEMORY                (TELEPHONY     << 1)
/* START ARC LOCAL */
#define LOCK_INSNS              (XYMEMORY      << 1)
#define SWAPE_INSN              (LOCK_INSNS    << 1)
#define RTSC_INSN               (SWAPE_INSN    << 1)
/* END ARC LOCAL */

static struct hash_control *arc_suffix_hash = NULL;

const char *md_shortopts = "";

enum options
{
  OPTION_EB = OPTION_MD_BASE,
  OPTION_EL,
  OPTION_A4,
  OPTION_A5,
  OPTION_ARC600,
  OPTION_ARC601,
  OPTION_ARC700,
  OPTION_ARCEM,
  OPTION_ARCHS,
  OPTION_MCPU,
  OPTION_USER_MODE,
  OPTION_LD_EXT_MASK,
  OPTION_SWAP,
  OPTION_NORM,
  OPTION_BARREL_SHIFT,
  OPTION_MIN_MAX,
  OPTION_NO_MPY,
  OPTION_EA,
  OPTION_MUL64,
  OPTION_SIMD,
  OPTION_SPFP,
  OPTION_DPFP,
  OPTION_XMAC_D16,
  OPTION_XMAC_24,
  OPTION_DSP_PACKA,
  OPTION_CRC,
  OPTION_DVBF,
  OPTION_TELEPHONY,
  OPTION_XYMEMORY,
  /* START ARC LOCAL */
  OPTION_LOCK,
  OPTION_SWAPE,
  OPTION_RTSC
  /* END ARC LOCAL */
/* ARC Extension library options.  */
};

struct option md_longopts[] =
{
  { "EB", no_argument, NULL, OPTION_EB },
  { "EL", no_argument, NULL, OPTION_EL },
  { "mA4", no_argument, NULL, OPTION_A4 },
  { "mA5", no_argument, NULL, OPTION_A5 },
  { "mA6", no_argument, NULL, OPTION_ARC600 },
  { "mARC600", no_argument, NULL, OPTION_ARC600 },
  { "mARC601", no_argument, NULL, OPTION_ARC601 },
  { "mARC700", no_argument, NULL, OPTION_ARC700 },
  { "mA7", no_argument, NULL, OPTION_ARC700 },
  { "mEM", no_argument, NULL, OPTION_ARCEM },
  { "mHS", no_argument, NULL, OPTION_ARCHS },
  { "mav2em", no_argument, NULL, OPTION_ARCEM },
  { "mav2hs", no_argument, NULL, OPTION_ARCHS },
  { "mcpu", required_argument, NULL, OPTION_MCPU },
  { "muser-mode-only", no_argument, NULL, OPTION_USER_MODE },
  { "mld-extension-reg-mask", required_argument, NULL, OPTION_LD_EXT_MASK },

/* ARC Extension library options.  */
  { "mswap", no_argument, NULL, OPTION_SWAP },
  { "mnorm", no_argument, NULL, OPTION_NORM },
  { "mbarrel-shifter", no_argument, NULL, OPTION_BARREL_SHIFT },
  { "mbarrel_shifter", no_argument, NULL, OPTION_BARREL_SHIFT },
  { "mmin-max", no_argument, NULL, OPTION_MIN_MAX },
  { "mmin_max", no_argument, NULL, OPTION_MIN_MAX },
  { "mno-mpy", no_argument, NULL, OPTION_NO_MPY },
  { "mea", no_argument, NULL, OPTION_EA },
  { "mEA", no_argument, NULL, OPTION_EA },
  { "mmul64", no_argument, NULL, OPTION_MUL64 },
  { "msimd", no_argument, NULL, OPTION_SIMD},
  { "mspfp", no_argument, NULL, OPTION_SPFP},
  { "mspfp-compact", no_argument, NULL, OPTION_SPFP},
  { "mspfp_compact", no_argument, NULL, OPTION_SPFP},
  { "mspfp-fast", no_argument, NULL, OPTION_SPFP},
  { "mspfp_fast", no_argument, NULL, OPTION_SPFP},
  { "mdpfp", no_argument, NULL, OPTION_DPFP},
  { "mdpfp-compact", no_argument, NULL, OPTION_DPFP},
  { "mdpfp_compact", no_argument, NULL, OPTION_DPFP},
  { "mdpfp-fast", no_argument, NULL, OPTION_DPFP},
  { "mdpfp_fast", no_argument, NULL, OPTION_DPFP},
  { "mmac-d16", no_argument, NULL, OPTION_XMAC_D16},
  { "mmac_d16", no_argument, NULL, OPTION_XMAC_D16},
  { "mmac-24", no_argument, NULL, OPTION_XMAC_24},
  { "mmac_24", no_argument, NULL, OPTION_XMAC_24},
  { "mdsp-packa", no_argument, NULL, OPTION_DSP_PACKA},
  { "mdsp_packa", no_argument, NULL, OPTION_DSP_PACKA},
  { "mcrc", no_argument, NULL, OPTION_CRC},
  { "mdvbf", no_argument, NULL, OPTION_DVBF},
  { "mtelephony", no_argument, NULL, OPTION_TELEPHONY},
  { "mxy", no_argument, NULL, OPTION_XYMEMORY},
  /* START ARC LOCAL */
  /* ARC700 4.10 extensions */
  { "mlock", no_argument, NULL, OPTION_LOCK},
  { "mswape", no_argument, NULL, OPTION_SWAPE},
  { "mrtsc", no_argument, NULL, OPTION_RTSC},
  /* END ARC LOCAL */
  { NULL, no_argument, NULL, 0 }
};

size_t md_longopts_size = sizeof (md_longopts);

#define IS_SYMBOL_OPERAND(o) \
   ((o) == 'g' || (o) == 'o' || (o) == 'M' || (o) == 'O' || (o) == 'R')

typedef enum
  {
    GOT_TYPE,
    PLT_TYPE,
    GOTOFF_TYPE,
    SDA_REF_TYPE,
    PCL_TYPE,
    TLSGD_TYPE,
    TLSIE_TYPE,
    TPOFF_TYPE,
    TPOFF9_TYPE,
    DTPOFF_TYPE,
    DTPOFF9_TYPE,
    NO_TYPE
  } arc700_special_symtype;

static arc700_special_symtype current_special_sym_flag;


/**************************************************************************/
/* Here's all the ARCompact illegal instruction sequence checking stuff.  */
/**************************************************************************/

#define MAJOR_OPCODE(x)  ((x & 0xf8000000) >> 27)
#define SUB_OPCODE(x)	 ((x & 0x003f0000) >> 16)

#define SUB_OPCODE2(x)	 (x & 0x0000003f)

#define SUB_OPCODE3(x)	 (((x & 0x07000000) >> 24) |   \
			  ((x & 0x00007000) >> 9))

#define J_INSN(x)	 ((MAJOR_OPCODE (x) == 0x4) && \
			  (SUB_OPCODE (x) >= 0x21) &&  \
			  (SUB_OPCODE (x) <= 0x22))

#define JL_INSN(x)	 ((MAJOR_OPCODE (x) == 0x4) && \
			  (SUB_OPCODE (x) >= 0x23) &&  \
			  (SUB_OPCODE (x) <= 0x24))

#define BLcc_INSN(x)     ((MAJOR_OPCODE (x) == 0x1) && \
			  ((x & 0x00010000) == 0))

#define Bcc_INSN(x)      (MAJOR_OPCODE (x) == 0x0)
#define BRcc_INSN(x)     ((MAJOR_OPCODE (x) == 0x1) &&	\
			  ((x & 0x00010000) == 1))

#define LP_INSN(x)	 ((MAJOR_OPCODE (x) == 0x4) && \
			  (SUB_OPCODE (x) == 0x28))

#define SLEEP_INSN(x)	 ((MAJOR_OPCODE (x) == 0x4) && \
			  (SUB_OPCODE (x) == 0x2f)  && \
			  (SUB_OPCODE2 (x) == 0x3f) && \
			  (SUB_OPCODE3 (x) == 0x01))

#define BRK_INSN(x)	 ((MAJOR_OPCODE (x) == 0x4) && \
			  (SUB_OPCODE (x) == 0x2f)  && \
			  (SUB_OPCODE2 (x) == 0x3f) && \
			  (SUB_OPCODE3 (x) == 0x05))

#define MAJOR16_OPCODE(x)  (x & 0x1F00)
#define SUB16_OPCODE2(x)   ((x & 0x00E0) >> 5)

#define Jx_S(x)            ((MAJOR16_OPCODE(x) == 0x0F) && \
			    (SUB16_OPCODE2(x) <= 0x03))

/* Data structures and functions for illegal instruction sequence
   checks. arc_insn last_two_insns is a queue of the last two instructions
   that have been assembled.  last_two_insns[0] is the head and
   last_two_insns[1] is the tail.  */

#define PREV_INSN_2 1
#define PREV_INSN_1 0

/* Queue containing the last two instructions seen.  */
static struct enriched_insn last_two_insns[2];

/* This is an "insert at front" linked list per Metaware spec
   that new definitions override older ones.  */
static struct arc_opcode *arc_ext_opcodes;

/* Flags to set in the elf header. */
static flagword arc_flags = 0x00;

/* Jump/Branch insn used to check if are the last insns in a ZOL */
struct insn_tuple
{
  /* The opcode itself.  Those bits which will be filled in with
     operands are zeroes.  */
  unsigned long opcode;

  /* The opcode mask.  This is used by the disassembler.  This is a
     mask containing ones indicating those bits which must match the
     opcode field, and zeroes indicating those bits which need not
     match (and are presumably filled in by operands).  */
  unsigned long mask;
};

static struct insn_tuple arcHS_checks[] = {
  { 0xF8010000, 0x0 },
  { 0xF8010010, 0x10000 },
  { 0xFE00, 0xF000 },
  { 0xFE00, 0xF200 },
  { 0xFE00, 0xF400 },
  { 0xFFC0, 0xF600 },
  { 0xFFC0, 0xF640 },
  { 0xFFC0, 0xF680 },
  { 0xFFC0, 0xF6C0 },
  { 0xFFC0, 0xF700 },
  { 0xFFC0, 0xF740 },
  { 0xFFC0, 0xF780 },
  { 0xFFC0, 0xF7C0 },
  { 0xF8030000, 0x8000000 },
  { 0xF8030010, 0x8020000 },
  { 0xF800, 0xF800 },
  { 0xF801001F, 0x8010000 },
  { 0xF801001F, 0x8010010 },
  { 0xF8010FFF, 0x8010F80 },
  { 0xFF01703F, 0xE017000 },
  { 0xFF01703F, 0xE017010 },
  { 0xF801001F, 0x8010001 },
  { 0xF801001F, 0x8010011 },
  { 0xF8010FFF, 0x8010F81 },
  { 0xFF01703F, 0xE017001 },
  { 0xFF01703F, 0xE017011 },
  { 0xF801001F, 0x8010002 },
  { 0xF801001F, 0x8010012 },
  { 0xF8010FFF, 0x8010F82 },
  { 0xFF01703F, 0xE017002 },
  { 0xFF01703F, 0xE017012 },
  { 0xF801001F, 0x8010003 },
  { 0xF801001F, 0x8010013 },
  { 0xF8010FFF, 0x8010F83 },
  { 0xFF01703F, 0xE017003 },
  { 0xFF01703F, 0xE017013 },
  { 0xF801001F, 0x8010004 },
  { 0xF801001F, 0x8010014 },
  { 0xF8010FFF, 0x8010F84 },
  { 0xFF01703F, 0xE017004 },
  { 0xFF01703F, 0xE017014 },
  { 0xF801001F, 0x8010005 },
  { 0xF801001F, 0x8010015 },
  { 0xF8010FFF, 0x8010F85 },
  { 0xFF01703F, 0xE017005 },
  { 0xFF01703F, 0xE017015 },
  { 0xF8010017, 0x8010000 },
  { 0xF8010017, 0x8010010 },
  { 0xF8010FF7, 0x8010F80 },
  { 0xFF017FF7, 0xE017F80 },
  { 0xFF017037, 0xE017000 },
  { 0xFF017037, 0xE017010 },
  { 0xF8010017, 0x8010001 },
  { 0xF8010017, 0x8010011 },
  { 0xF8010FF7, 0x8010F81 },
  { 0xFF017FF7, 0xE017F81 },
  { 0xFF017037, 0xE017001 },
  { 0xFF017037, 0xE017011 },
  { 0xF8010017, 0x8010002 },
  { 0xF8010017, 0x8010012 },
  { 0xF8010FF7, 0x8010F82 },
  { 0xFF017FF7, 0xE017F82 },
  { 0xFF017037, 0xE017002 },
  { 0xFF017037, 0xE017012 },
  { 0xF8010017, 0x8010003 },
  { 0xF8010017, 0x8010013 },
  { 0xF8010FF7, 0x8010F83 },
  { 0xFF017FF7, 0xE017F83 },
  { 0xFF017037, 0xE017003 },
  { 0xFF017037, 0xE017013 },
  { 0xF8010017, 0x8010004 },
  { 0xF8010017, 0x8010014 },
  { 0xF8010FF7, 0x8010F84 },
  { 0xFF017FF7, 0xE017F84 },
  { 0xFF017037, 0xE017004 },
  { 0xFF017037, 0xE017014 },
  { 0xF8010017, 0x8010005 },
  { 0xF8010017, 0x8010015 },
  { 0xF8010FF7, 0x8010F85 },
  { 0xFF017FF7, 0xE017F85 },
  { 0xFF017037, 0xE017005 },
  { 0xFF017037, 0xE017015 },
  { 0xF880, 0xE880 },
  { 0xF880, 0xE800 },
  { 0xFFFFFFFF, 0x256F003F },
  { 0xFFFF, 0x7FFF },
  { 0xF801001F, 0x801000E },
  { 0xF801001F, 0x801001E },
  { 0xF8010FFF, 0x8010F8E },
  { 0xFF01703F, 0xE01700E },
  { 0xFF01703F, 0xE01701E },
  { 0xFF017FFF, 0xE017F8E },
  { 0xF8010017, 0x8010006 },
  { 0xF8010017, 0x8010016 },
  { 0xF8010FF7, 0x8010F86 },
  { 0xFF017037, 0xE017006 },
  { 0xFF017037, 0xE017016 },
  { 0xFF017FF7, 0xE017F86 },
  { 0xF801001F, 0x801000F },
  { 0xF801001F, 0x801001F },
  { 0xF8010FFF, 0x8010F8F },
  { 0xFF01703F, 0xE01700F },
  { 0xFF01703F, 0xE01701F },
  { 0xFF017FFF, 0xE017F8F },
  { 0xF8010017, 0x8010007 },
  { 0xF8010017, 0x8010017 },
  { 0xF8010FF7, 0x8010F87 },
  { 0xFF017037, 0xE017007 },
  { 0xFF017037, 0xE017017 },
  { 0xFF017FF7, 0xE017F87 },
  { 0xFFFFF03F, 0x20200000 },
  { 0xFFFFFFFF, 0x202007C0 },
  { 0xFFFFFFFF, 0x20200F80 },
  { 0xFFFFFFFF, 0x20208740 },
  { 0xFFFFFFFF, 0x20208780 },
  { 0xFFFFF03F, 0x20600000 },
  { 0xFFFFF000, 0x20A00000 },
  { 0xFFFFF020, 0x20E00000 },
  { 0xFFFFFFE0, 0x20E007C0 },
  { 0xFFFFFFE0, 0x20E00F80 },
  { 0xFFFFFFE0, 0x20E08740 },
  { 0xFFFFFFE0, 0x20E08780 },
  { 0xFFFFF020, 0x20E00020 },
  { 0xFFFFF03F, 0x20210000 },
  { 0xFFFFFFFF, 0x202107C0 },
  { 0xFFFFF03F, 0x20610000 },
  { 0xFFFFF000, 0x20A10000 },
  { 0xFFFFF020, 0x20E10000 },
  { 0xFFFFFFE0, 0x20E107C0 },
  { 0xFFFFF020, 0x20E10020 },
  { 0xF8FF, 0x7800 },
  { 0xF8FF, 0x7820 },
  { 0xFFFF, 0x7EE0 },
  { 0xFFFF, 0x7FE0 },
  { 0xFFFF, 0x7CE0 },
  { 0xFFFF, 0x7DE0 },
  { 0xFFFFF03F, 0x20200000 },
  { 0xFFFFFFFF, 0x202007C0 },
  { 0xFFFFFFFF, 0x20200F80 },
  { 0xFFFFF03F, 0x20600000 },
  { 0xFFFFF000, 0x20A00000 },
  { 0xFFFFF020, 0x20E00000 },
  { 0xFFFFFFE0, 0x20E007C0 },
  { 0xFFFFFFE0, 0x20E00F80 },
  { 0xFFFFF020, 0x20E00020 },
  { 0xFFFFF03F, 0x20210000 },
  { 0xFFFFFFFF, 0x202107C0 },
  { 0xFFFFF03F, 0x20610000 },
  { 0xFFFFF000, 0x20A10000 },
  { 0xFFFFF020, 0x20E10000 },
  { 0xFFFFFFE0, 0x20E107C0 },
  { 0xFFFFF020, 0x20E10020 },
  { 0xF8FF, 0x7800 },
  { 0xF8FF, 0x7820 },
  { 0xFFFF, 0x7EE0 },
  { 0xFFFF, 0x7FE0 },
  { 0xFFFF, 0x7CE0 },
  { 0xFFFF, 0x7DE0 },
  { 0xFFFFF03F, 0x20220000 },
  { 0xFFFFFFFF, 0x20220F80 },
  { 0xFFFFF03F, 0x20620000 },
  { 0xFFFFF000, 0x20A20000 },
  { 0xFFFFF020, 0x20E20000 },
  { 0xFFFFFFE0, 0x20E20F80 },
  { 0xFFFFF020, 0x20E20020 },
  { 0xFFFFF03F, 0x20230000 },
  { 0xFFFFF03F, 0x20630000 },
  { 0xFFFFF000, 0x20A30000 },
  { 0xFFFFF020, 0x20E30000 },
  { 0xFFFFF020, 0x20E30020 },
  { 0xF8FF, 0x7840 },
  { 0xF8FF, 0x7860 },
  { 0xFFFFF03F, 0x20220000 },
  { 0xFFFFFFFF, 0x20220F80 },
  { 0xFFFFF03F, 0x20620000 },
  { 0xFFFFF000, 0x20A20000 },
  { 0xFFFFF020, 0x20E20000 },
  { 0xFFFFFFE0, 0x20E20F80 },
  { 0xFFFFF020, 0x20E20020 },
  { 0xFFFFF03F, 0x20230000 },
  { 0xFFFFF03F, 0x20630000 },
  { 0xFFFFF000, 0x20A30000 },
  { 0xFFFFF020, 0x20E30000 },
  { 0xFFFFF020, 0x20E30020 },
  { 0xF8FF, 0x7840 },
  { 0xF8FF, 0x7860 },
  { 0xFC00, 0x5800 }
};

/* helper zero_overhead_checks. */
static int check_last_ZOL_insn (arc_insn insn)
{
  int i;
  for (i = 0; i < sizeof (arcHS_checks)/sizeof (arcHS_checks[0]); i++)
    {
      /* Avoid checking short instruction vs long masks, can introduce
	 false positive. */
      if (((insn & 0xFFFF0000) == 0)
	  && ((arcHS_checks[i].opcode & 0xFFFF0000) != 0))
	continue;

      if ((arcHS_checks[i].opcode & insn) == arcHS_checks[i].mask)
	return 1;
    }
  return 0;
}

static void zero_overhead_checks (struct loop_target *);
static void insert_last_insn (arc_insn insn,
			      unsigned short delay_slot,
			      unsigned short limm,
			      symbolS *sym)
{
  last_two_insns[PREV_INSN_2]=last_two_insns[PREV_INSN_1];
  last_two_insns[PREV_INSN_1].insn=insn;
  last_two_insns[PREV_INSN_1].delay_slot=delay_slot;
  last_two_insns[PREV_INSN_1].limm=limm;
  if (LP_INSN (insn))
    last_two_insns[PREV_INSN_1].sym= symbol_get_bfdsym (sym);
  else
    last_two_insns[PREV_INSN_1].sym = NULL;
}


/* labelsym and lt->symbol form a commutative pair of symbol of the label
   definition and the symbol of the label use.  This function needs to be
   called only when we've identified a loop completely, ie. found both the
   head (the defining lpcc instruction) and the tail (loop ending label).  */
static void zero_overhead_checks (struct loop_target *lt)
{

  switch (arc_mach_type)
    {
    case bfd_mach_arc_a5:

      /* This takes care of insn being Jcc.d, Bcc.d, JCcc.d, BRcc.d,
	 BBITn.d, J_S.d.  */
      if (lt->prev_two_insns[PREV_INSN_1].delay_slot ||
	  BLcc_INSN (lt->prev_two_insns[PREV_INSN_1].insn) ||
	  JL_INSN (lt->prev_two_insns[PREV_INSN_1].insn))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      /* We haven't handled JL_S.d in insn-1 of the loop.  */
      if (lt->prev_two_insns[PREV_INSN_2].delay_slot)
	if (JL_INSN (lt->prev_two_insns[PREV_INSN_2].insn) ||
	    BLcc_INSN (lt->prev_two_insns[PREV_INSN_2].insn))
	  as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      /* This takes care of JLcc limm.  */
      if (lt->prev_two_insns[PREV_INSN_2].limm)
	if (JL_INSN (lt->prev_two_insns[PREV_INSN_2].insn))
	  as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      /* This takes care of LP other_loop in insn and insn-1.  */
      if ((LP_INSN (lt->prev_two_insns[PREV_INSN_1].insn) &&
	   lt->prev_two_insns[PREV_INSN_1].sym != lt->symbol) ||
	  (LP_INSN (lt->prev_two_insns[PREV_INSN_2].insn) &&
	   lt->prev_two_insns[PREV_INSN_2].sym != lt->symbol))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      if (SLEEP_INSN (lt->prev_two_insns[PREV_INSN_1].insn) ||
	  BRK_INSN (lt->prev_two_insns[PREV_INSN_1].insn))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      break;

    case bfd_mach_arc_arc600:

      if (BLcc_INSN (lt->prev_two_insns[PREV_INSN_1].insn) ||
	  JL_INSN (lt->prev_two_insns[PREV_INSN_1].insn))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      /* This takes care of LP other_loop in insn and insn-1.
       * Also check for single intervening instruction without a long
       * immediate operand.
       */
      if (LP_INSN (lt->prev_two_insns[PREV_INSN_1].insn) ||
	  (LP_INSN (lt->prev_two_insns[PREV_INSN_2].insn) &&
	   !lt->prev_two_insns[PREV_INSN_1].limm))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      if (SLEEP_INSN (lt->prev_two_insns[PREV_INSN_1].insn) ||
	  BRK_INSN (lt->prev_two_insns[PREV_INSN_1].insn))
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      if (lt->prev_two_insns[PREV_INSN_2].limm)
	if (JL_INSN (lt->prev_two_insns[PREV_INSN_2].insn))
	  as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      if (lt->prev_two_insns[PREV_INSN_1].limm)
	if (J_INSN (lt->prev_two_insns[PREV_INSN_1].insn))
	  as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      if (lt->prev_two_insns[PREV_INSN_1].delay_slot)
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      /* We haven't handled JL_S.d in insn-1 of the loop.  */
      if (lt->prev_two_insns[PREV_INSN_2].delay_slot)
	if (JL_INSN (lt->prev_two_insns[PREV_INSN_2].insn) ||
	    BLcc_INSN (lt->prev_two_insns[PREV_INSN_2].insn))
	  as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      break;

    case bfd_mach_arc_arcv2:
      if ((arc_flags & EF_ARC_MACH_MSK) == EF_ARC_CPU_ARCV2HS)
	{
	  arc_insn insn = lt->prev_two_insns[PREV_INSN_1].insn;
	  if (check_last_ZOL_insn (insn))
	    as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

	  insn = lt->prev_two_insns[PREV_INSN_2].insn;
	  if (lt->prev_two_insns[PREV_INSN_2].delay_slot
	      && check_last_ZOL_insn (insn))
	    as_bad ("An instruction of this type may not be executed in this \
instruction slot.");
	}
    case bfd_mach_arc_arc700:

      if (lt->prev_two_insns[PREV_INSN_1].delay_slot)
	as_bad ("An instruction of this type may not be executed in this \
instruction slot.");

      break;

    default:
      ;

    }
}

static void add_loop_target (symbolS *symbol)
{
  struct loop_target *tmp = &symbol_get_tc (symbol)->loop_target;

  if (!tmp->symbol)
    {
      tmp->symbol = symbol_get_bfdsym (symbol);
    }
  else
    {
      zero_overhead_checks (tmp);
    }
}

void
arc_check_label (symbolS *labelsym)
{
  /* At this point, the current line pointer is sitting on the character
     just after the first colon on the label.  */

  struct loop_target *tmp = &symbol_get_tc (labelsym)->loop_target;
  asymbol *new;

  new = symbol_get_bfdsym (labelsym);

  /* Label already defined.  */
  if (tmp->symbol)
    {
      /* Store the last two instructions.  */
      tmp->prev_two_insns[PREV_INSN_1]=last_two_insns[PREV_INSN_1];
      tmp->prev_two_insns[PREV_INSN_2]=last_two_insns[PREV_INSN_2];

      /* Now perform whatever checks on these last two instructions.  */
      zero_overhead_checks (tmp);
    }
  /* Label not defined.  */
  else
    {
      tmp->symbol = new;

      /* Store the last two instructions.  */
      tmp->prev_two_insns[PREV_INSN_1]=last_two_insns[PREV_INSN_1];
      tmp->prev_two_insns[PREV_INSN_2]=last_two_insns[PREV_INSN_2];
    }
}

/* Here's where all the ARCompact illegal instruction sequence checks end.  */

/*
 * md_parse_option
 *
 * Invocation line includes a switch not recognized by the base assembler.
 * See if it's a processor-specific option.
 */

int
md_parse_option (int c, char *arg)
{
  int cpu_flags = EF_ARC_CPU_GENERIC;

  switch (c)
    {
    case OPTION_A4:
	as_warn(_("Obsolete option mA4"));
      break;
    case OPTION_A5:
      cpu_flags = E_ARC_MACH_A5;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_a5;
      break;
    case OPTION_ARC600:
      cpu_flags = E_ARC_MACH_ARC600;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_arc600;
      break;
    case OPTION_ARC601:
      cpu_flags = E_ARC_MACH_ARC601;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_arc601;
      break;
    case OPTION_ARC700:
      cpu_flags = E_ARC_MACH_ARC700;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_arc700;
      break;
    case OPTION_ARCHS:
      cpu_flags = EF_ARC_CPU_ARCV2HS;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_arcv2;
      arc_mach_a4= 0;
      break;
    case OPTION_ARCEM:
      cpu_flags = EF_ARC_CPU_ARCV2EM;
      mach_type_specified_p = 1;
      arc_mach_type = bfd_mach_arc_arcv2;
      break;
    case OPTION_MCPU:
      if (arg == NULL)
	as_warn (_("No CPU identifier specified"));
      else if (strcmp (arg, "ARC600") == 0)
	return md_parse_option (OPTION_ARC600, NULL);
      else if (strcmp (arg, "ARC601") == 0)
	return md_parse_option (OPTION_ARC601, NULL);
      else if (strcmp (arg, "ARC700") == 0)
	return md_parse_option (OPTION_ARC700, NULL);
      else if (strcmp (arg, "ARCv2EM") == 0)
	return md_parse_option (OPTION_ARCEM, NULL);
      else if (strcmp (arg, "ARCv2HS") == 0)
	return md_parse_option (OPTION_ARCHS, NULL);
      else
	as_warn(_("Unknown CPU identifier `%s'"), arg);
      break;
    case OPTION_USER_MODE:
      arc_user_mode_only = 1;
      break;
    case OPTION_LD_EXT_MASK:
      arc_ld_ext_mask = strtoul (arg, NULL, 0);
      break;
    case OPTION_EB:
      byte_order = BIG_ENDIAN;
      arc_target_format = "elf32-bigarc";
      break;
    case OPTION_EL:
      byte_order = LITTLE_ENDIAN;
      arc_target_format = "elf32-littlearc";
      break;
    case OPTION_SWAP:
      extinsnlib |= SWAP_INSN;
      break;
    case OPTION_NORM:
      extinsnlib |= NORM_INSN;
      break;
    case OPTION_BARREL_SHIFT:
      extinsnlib |= BARREL_SHIFT_INSN;
      break;
    case OPTION_MIN_MAX:
      extinsnlib |= MIN_MAX_INSN;
      break;
    case OPTION_NO_MPY:
      extinsnlib |= NO_MPY_INSN;
      break;
    case OPTION_EA:
      extinsnlib |= EA_INSN;
      break;
    case OPTION_MUL64:
      extinsnlib |= MUL64_INSN;
      break;
    case OPTION_SIMD:
      extinsnlib |= SIMD_INSN;
      break;
    case OPTION_SPFP:
      extinsnlib |= SP_FLOAT_INSN;
      break;
    case OPTION_DPFP:
      extinsnlib |= DP_FLOAT_INSN;
      break;
    case OPTION_XMAC_D16:
      extinsnlib |= XMAC_D16;
      break;
    case OPTION_XMAC_24:
      extinsnlib |= XMAC_24;
      break;
    case OPTION_DSP_PACKA:
      extinsnlib |= DSP_PACKA;
      break;
    case OPTION_CRC:
      extinsnlib |= CRC;
      break;
    case OPTION_DVBF:
      extinsnlib |= DVBF;
      break;
    case OPTION_TELEPHONY:
      extinsnlib |= TELEPHONY;
      break;
    case OPTION_XYMEMORY:
      extinsnlib |= XYMEMORY;
      break;
    /* START ARC LOCAL */
    case OPTION_LOCK:
      extinsnlib |= LOCK_INSNS;
      break;
    case OPTION_SWAPE:
      extinsnlib |= SWAPE_INSN;
      break;
    case OPTION_RTSC:
      extinsnlib |= RTSC_INSN;
      break;
    /* END ARC LOCAL */

    default:
      return 0;
    }

  if (cpu_flags != EF_ARC_CPU_GENERIC)
    arc_flags = (arc_flags & ~EF_ARC_MACH_MSK) | cpu_flags;

  return 1;
}

void
md_show_usage (FILE *stream)
{
  fprintf (stream, "\
ARC Options:\n\
  -mA[4|5]                select processor variant (default arc%d)\n\
  -mARC[600|700]          select processor variant\n\
  -mEM|-mav2em            select ARCv2 EM processor variant\n\
  -mHS|-mav2hs            select ARCv2 HS processor variant\n\
  -EB                     assemble code for a big endian cpu\n\
  -EL                     assemble code for a little endian cpu\n", arc_mach_type + 4);
}

/* Extension library support. */

extern char *myname;

/* There are two directories in which the binary in the install
   directory.  We would need to use the appropriate binary directory
   depending on which of the binaries is being executed.  */

#define BINDIR1 "/home/bin"
#define BINDIR2 "/home/arc-elf32/bin"
#define LIBDIR1 "/home/extlib"

#define BINDIR3 "/gas"
#define BINDIR4 "/gas/.libs"
#define LIBDIR2 "/gas/config/extlib"
#define EXTLIBFILE "arcextlib.s"
#define SIMDEXTLIBFILE "arcsimd.s"

struct extension_macro
{
  unsigned long option;
  char name[20];
};

static struct extension_macro extension_macros[]=
  {
    {SWAP_INSN, "__Xswap"},
    {NORM_INSN, "__Xnorm"},
    {BARREL_SHIFT_INSN,"__Xbarrel_shifter"},
    {MIN_MAX_INSN,"__Xmin_max"},
    {NO_MPY_INSN,"__Xno_mpy"},
    {EA_INSN,"__Xea"},
    {MUL64_INSN,"__Xmult32"},
    {SIMD_INSN, "__Xsimd"},
    {SP_FLOAT_INSN, "__Xspfp"},
    {DP_FLOAT_INSN, "__Xdpfp"},
    {XMAC_D16, "__Xxmac_d16"},
    {XMAC_24, "__Xxmac_24"},
    {DSP_PACKA, "__Xdsp_packa"},
    {CRC, "__Xcrc"},
    {DVBF, "__Xdvbf"},
    {TELEPHONY, "__Xtelephony"},
    {XYMEMORY, "__Xxy"},
    /* START ARC LOCAL */
    {LOCK_INSNS, "__Xlock"},
    {SWAPE_INSN, "__Xswape"},
    {RTSC_INSN, "__Xrtsc"}
    /* END ARC LOCAL */
  };

static unsigned short n_extension_macros = (sizeof (extension_macros) /
				   sizeof (struct extension_macro));

static int
file_exists (char *filename)
{
  FILE *fp = fopen (filename , "r");

  if (fp)
    {
      fclose (fp);
      return 1;
    }
  else
    return 0;
}

/* This function reads in the "configuration files" based on the options
   passed on the command line through the options -mswap and -mnorm.  */
static void
arc_process_extinstr_options (void)
{
  unsigned long i;
  char extension_library_path[160];
  char temp[80];
  symbolS *sym;

  /* Let's get to the right extension configuration library based on which
     processor we are assembling the source assembly file for.  */

  switch (arc_mach_type)
    {
    case bfd_mach_arc_a5:
      strcpy (temp, "__A5__");
      break;

    case bfd_mach_arc_arc600:
      strcpy (temp, "__ARC600__");
      break;

    case bfd_mach_arc_arc601:
      strcpy (temp, "__ARC601__");
      break;

    case bfd_mach_arc_arc700:
      strcpy (temp, "__ARC700__");
      break;

    case bfd_mach_arc_arcv2:
      strcpy (temp, "__ARCv2__");
      break;

    default:
      as_bad ("Oops! Something went wrong here!");
      break;
    }

  /*For ARCv2EM disable all lib extensions (Except the FP ops). All
    ARCv2 instructions are recognized by default by the GAS*/
  if ((arc_mach_type == bfd_mach_arc_arcv2)
      && (extinsnlib & ~(SP_FLOAT_INSN | DP_FLOAT_INSN)))
    {
      extinsnlib &= (SP_FLOAT_INSN | DP_FLOAT_INSN);
    }

  if ((extinsnlib & NO_MPY_INSN) && (arc_mach_type != bfd_mach_arc_arc700))
    {
      as_bad ("-mno-mpy can only be used with ARC700");
      exit (1);
    }

  if ((extinsnlib & MUL64_INSN) && (arc_mach_type == bfd_mach_arc_arc700))
    {
      as_bad ("-mmul64 cannot be used with ARC 700");
      exit (1);
    }

  if ((extinsnlib & SIMD_INSN ) && ( arc_mach_type != bfd_mach_arc_arc700))
    {
      as_bad ("-msimd can only be used with ARC 700");
      exit (1);
    }

  /* START ARC LOCAL */
  if ((extinsnlib & LOCK_INSNS ) && ( arc_mach_type != bfd_mach_arc_arc700))
    {
      as_bad ("-mlock and -mscond can only be used with ARC 700 v4.10");
      exit (1);
    }

  if ((extinsnlib & SWAPE_INSN ) && ( arc_mach_type != bfd_mach_arc_arc700))
    {
      as_bad ("-mswape can only be used with ARC 700 v4.10");
      exit (1);
    }

  if ((extinsnlib & RTSC_INSN ) && ( arc_mach_type != bfd_mach_arc_arc700))
    {
      as_bad ("-mrtsc can only be used with ARC 700 v4.10");
      exit (1);
    }
  /* END ARC LOCAL */

  sym = (symbolS *) local_symbol_make (temp, absolute_section, 1,
				       &zero_address_frag);
  symbol_table_insert (sym);

  for (i=0 ; i < n_extension_macros ; ++i)
    {
      if (extinsnlib & extension_macros[i].option)
	{
	  sym = (symbolS *) local_symbol_make (extension_macros[i].name,
					       absolute_section, 1,
					       &zero_address_frag);
	  symbol_table_insert (sym);
	}
    }


  /* Let's get the path of the base directory of the extension configuration
  libraries.  */

  strcpy (extension_library_path,
	  make_relative_prefix (myname, BINDIR1, LIBDIR1));
  strcat (extension_library_path, "/"EXTLIBFILE);

  if (!file_exists (extension_library_path))
    {
      strcpy (extension_library_path,
	      make_relative_prefix (myname, BINDIR2, LIBDIR1));
      strcat (extension_library_path, "/"EXTLIBFILE);
    }

  if (!file_exists (extension_library_path))
    {
      strcpy (extension_library_path,
	      make_relative_prefix (myname, BINDIR3, LIBDIR2));
      strcat (extension_library_path, "/"EXTLIBFILE);
    }

  if (!file_exists (extension_library_path))
    {
      strcpy (extension_library_path,
	      make_relative_prefix (myname, BINDIR4, LIBDIR2));
      strcat (extension_library_path, "/"EXTLIBFILE);
    }

  if (!file_exists (extension_library_path))
    {
      as_bad ("Extension library file(s) do not exist\n");
      exit (1);
    }

  /* For A4, A5 and ARC600 read the lib file if extinsnlib is set
     For ARC700 do not read the lib file if the NO_MPY_INSN flag
     is the only one set*/
  if ( (arc_mach_type == bfd_mach_arc_arc700)?
       (extinsnlib != NO_MPY_INSN)
       : extinsnlib)
    read_a_source_file (extension_library_path);

  if (extinsnlib &  SIMD_INSN)
    {
      strcpy (extension_library_path,
	      make_relative_prefix (myname, BINDIR1, LIBDIR1));
      strcat (extension_library_path, "/"SIMDEXTLIBFILE);

      if (!file_exists (extension_library_path))
	{
	  strcpy (extension_library_path,
		  make_relative_prefix (myname, BINDIR2, LIBDIR1));
	  strcat (extension_library_path, "/"SIMDEXTLIBFILE);
	}

      if (!file_exists (extension_library_path))
	{
	  strcpy (extension_library_path,
		  make_relative_prefix (myname, BINDIR3, LIBDIR2));
	  strcat (extension_library_path, "/"SIMDEXTLIBFILE);
	}

      if (!file_exists (extension_library_path))
	{
	  as_bad ("ARC700 SIMD Extension library file(s) do not exist\n");
	  exit (1);
	}

      read_a_source_file (extension_library_path );
    }
}

/* This function is called once, at assembler startup time.  It should
   set up all the tables, etc. that the MD part of the assembler will need.
   Opcode selection is deferred until later because we might see a .option
   command.  */

void
md_begin (void)
{
  arc_mach_a4 = 0;

  /* The endianness can be chosen "at the factory".  */
  target_big_endian = byte_order == BIG_ENDIAN;

  if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, arc_mach_type))
    as_warn ("could not set architecture and machine");

  if (arc_flags)
    bfd_set_private_flags (stdoutput, arc_flags);

  /* Assume the base cpu.  This call is necessary because we need to
     initialize `arc_operand_map' which may be needed before we see the
     first insn.  */
  arc_opcode_init_tables (arc_get_opcode_mach (arc_flags ? arc_flags : arc_mach_type,
					       target_big_endian));

  arc_process_extinstr_options ();
}

/* Initialize the various opcode and operand tables.
   MACH is one of bfd_mach_arc_xxx.  */

static void
init_opcode_tables (int mach)
{
  int i;
  char *last;

  if ((arc_suffix_hash = hash_new ()) == NULL)
    as_fatal ("virtual memory exhausted");

  if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, mach))
    as_warn ("could not set architecture and machine");

  /* This initializes a few things in arc-opc.c that we need.
     This must be called before the various arc_xxx_supported fns.  */
  arc_opcode_init_tables (arc_get_opcode_mach (arc_flags ? arc_flags : mach,
					       target_big_endian));

  /* Only put the first entry of each equivalently named suffix in the
     table.  */
  last = "";
  for (i = 0; i < arc_suffixes_count; i++)
    {
	/*
	  A check using arc_opval_supported is omitted in the 2.15
	  Not adding it until required.
	*/
      if (strcmp (arc_suffixes[i].name, last) != 0)
	hash_insert (arc_suffix_hash, arc_suffixes[i].name, (void *) (arc_suffixes + i));
      last = arc_suffixes[i].name;
    }

  /* Tell `.option' it's too late.  */
  cpu_tables_init_p = 1;
}

/* Insert an operand value into an instruction.
   If REG is non-NULL, it is a register number and ignore VAL.  */

static arc_insn
arc_insert_operand (arc_insn insn, long *insn2,
		    const struct arc_operand *operand,
		    int mods,
		    const struct arc_operand_value *reg,
		    offsetT val,
		    char *file,
		    unsigned int line)
{
  if (operand->bits != 32)
    {
      long min, max, bits;
      offsetT test;

      if (operand->flags & ARC_OPERAND_4BYTE_ALIGNED)
	bits = operand->bits + 2;
      else if (operand->flags & ARC_OPERAND_2BYTE_ALIGNED)
	bits = operand->bits + 1;
      else
	bits = operand->bits;

      if ((operand->flags & ARC_OPERAND_SIGNED) != 0)
	{
	  if ((operand->flags & ARC_OPERAND_SIGNOPT) != 0)
	    max = (1 << bits) - 1;
	  else
	    max = (1 << (bits - 1)) - 1;
	  min = - (1 << (bits - 1));
	}
      else
	{
	  max = (1 << bits) - 1;
	  min = 0;
	}

      if ((operand->flags & ARC_OPERAND_NEGATIVE) != 0)
	test = - val;
      else
	test = val;

      if (test < (offsetT) min || test > (offsetT) max)
	((operand->flags & ARC_OPERAND_ERROR
	  ? as_bad_value_out_of_range : as_warn_value_out_of_range)
	 (_("operand"), test, (offsetT) min, (offsetT) max, file, line));
    }

  if (operand->insert)
    {
      const char *errmsg;

      errmsg = NULL;
      insn = (*operand->insert) (insn,insn2, operand, mods, reg, (long) val, &errmsg);
      if (errmsg != (const char *) NULL)
	{
	  /* Yuk! This is meant to have a literal argument, so it can be
	     translated. We add a dummy empty string argument, which keeps
	     -Wformat-security happy for now. */
	  as_warn (errmsg, "");
	}
    }
  else
    insn |= (((long) val & ((1 << operand->bits) - 1))
	     << operand->shift);

  return insn;
}

/* We need to keep a list of fixups.  We can't simply generate them as
   we go, because that would require us to first create the frag, and
   that would screw up references to ``.''.  */

struct arc_fixup
{
  /* index into `arc_operands'  */
  int opindex;
  unsigned int modifier_flags;
  expressionS exp;
};

#define MAX_FIXUPS 5

#define MAX_SUFFIXES 5

/* Compute the reloc type of an expression EXP.

   DEFAULT_TYPE is the type to use if no special processing is required,
   and PCREL_TYPE the one for pc-relative relocations.  */

static int
get_arc_exp_reloc_type (int default_type,
			int pcrel_type,
			expressionS *exp)
{
  if (default_type == BFD_RELOC_32
      && exp->X_op == O_subtract
      && exp->X_op_symbol != NULL
      && exp->X_op_symbol->bsym->section == now_seg)
    return pcrel_type;
  return default_type;
}

/* n1 - syntax type (3 operand, 2 operand, 1 operand or 0 operand).
	core register number.
	auxillary register number.
	condition code.
   n2 - major opcode if instruction.
   n2 - index to special type if core register.

   n3 - subopcode.
	If n1 has AC_SYNTAX_SIMD set then n3 has additional fields packed.
	bits  usage
	0-7    sub_opcode
	8-15   real opcode if EXTEND2
	16-23  real opcode if EXTEND3
	24-25  accumulator mode
	28-29  nops
	30-31  flag1 and flag2
*/

static void
arc_set_ext_seg (enum ExtOperType type, int n1, int n2, int n3)
{
  int nn2;
  char *type_str;
  char type_strings[][8] = {"inst","core","aux","cond","corereg"};
  char temp[15];
  char *aamode,*efmode;

  char section_name[80];

  /* Generate section names based on the type of extension map record.  */

  aamode = "";
  efmode = "";
  switch (type)
    {

    case EXT_AC_INSTRUCTION:
    case EXT_INSTRUCTION:

      type_str = type_strings[0];
      switch (n1)
	{
	case AC_SYNTAX_3OP:
	case SYNTAX_3OP:
	  n1 = 3;
	  break;
	case AC_SYNTAX_2OP:
	case SYNTAX_2OP:
	  n1 = 2;
	  break;
	case AC_SYNTAX_1OP:
	case SYNTAX_1OP:
	  n1 = 1;
	  break;
	case AC_SYNTAX_NOP:
	case SYNTAX_NOP:
	  n1 = 0;
	  break;
	default:
	  n1 = (n3 >> 28) & 0x3;
	  switch((n3 >> 24) & 0x3){
	  case 0:
	      aamode = "aa0";
	      break;
	  case 1:
	      aamode = "aa1";
	      extended_instruction_flags[2] |= (FLAG_AS >> 8);
	      break;
	  case 2:
	      aamode = "aa2";
	      extended_instruction_flags[2] |= (FLAG_AP >> 8);
	      break;
	  case 3:
	      aamode = "aa3";
	      extended_instruction_flags[2] |= (FLAG_AM >> 8);
	      break;
	      }
	  switch((n3 >> 30) & 0x3){
	  case 0:
	      efmode = ".ef0";
	      break;
	  case 1:
	      efmode = ".ef1";
	      extended_instruction_flags[1] |= (FLAG_FMT1 >> 16);
	      break;
	  case 2:
	      efmode = ".ef2";
	      extended_instruction_flags[1] |= (FLAG_FMT2 >> 16);
	      break;
	  case 3:
	      efmode = ".ef3";
	      extended_instruction_flags[1] |= (FLAG_FMT3 >> 16);
	      break;
	      }
	  ;
	} /* end switch(n1) */

      nn2 = n3 & 0x3f;
      if(n2 == 0xa || n2 == 0x9){
	  if(nn2 == 0x2f){
	      nn2 = (n3 >> 8) & 0x3f;
	      if(nn2 == 0x3f){
		  nn2 = (n3 >> 16) & 0x3f;
		  }
	      }
	  }
      if(n2 == 0x9){
	  if(n3 & 0x4000000){
	      extended_instruction_flags[3] |= FLAG_3OP_U8;
	      sprintf(temp, "%d.%d.%du8", n1, n2, nn2&0x3f);
	      }
	  else
	      sprintf (temp, "%d.%d.%d", n1, n2, nn2&0x3f);
	  }
      else
	/* Note this can grow to be 13 characters wide, make
	   sure TEMP above is accommodating. */
	  sprintf (temp, "%d.%d.%d%s%s", n1, n2, nn2&0x3f,aamode,efmode);
      break;

    case EXT_LONG_CORE_REGISTER:
      type_str = type_strings[4];
      if(n2!=0)
	  sprintf(temp,"%d%c",n1,n2);
      else
	  sprintf (temp, "%d", n1);
      break;

    case EXT_CORE_REGISTER:
      type_str = type_strings[1];
      if(n2!=0)
	  sprintf(temp,"%d%c",n1,n2);
      else
	  sprintf (temp, "%d", n1);
      break;

    case EXT_AUX_REGISTER:
      type_str = type_strings[2];
      sprintf (temp, "%d", n1);
      break;

    case EXT_COND_CODE:
      type_str = type_strings[3];
      sprintf (temp, "%d", n1);
      break;

    default:
      abort ();

    } /* end switch(type) */
  sprintf (section_name, ".gnu.linkonce.arcextmap.%s.%s", type_str, temp);
  if (!symbol_find (section_name) || (n2 !=9 &&n2 !=10)){
      symbolS *sym;
      sym = (symbolS *) local_symbol_make (section_name,
				       absolute_section, 1,
				       &zero_address_frag);
      symbol_table_insert (sym);
      arcext_section = subseg_new (xstrdup (section_name), 0);
      bfd_set_section_flags (stdoutput, arcext_section,
			 SEC_READONLY | SEC_HAS_CONTENTS);
      }
  else {
      use_extended_instruction_format = 2;
      }
}

/* process extension condition code
 * format   .extCondCode   name,value
 *      name   is name of condition code.
 *      value  is value of condition code.
 * extension core register.
 * format   .extCoreRegister name,value,mode,shortcut
 *      name   is name of register.
 *      value  is register number.
 *      mode   is r,w,r|w,w|r or blank for Read/Write usage.
 *      shortcut  can_shortcut
 *                cannot_shortcut
 * extension auxiliary register.
 * format    .extAuxRegister name,value,mode
 *      name   is name of register.
 *      value  is register number.
 *      r,w,r|w,w|r, or blank for Read/Write usage.
 */
static void
arc_extoper (int opertype)
{
  char *name;
  char *mode;
  char c;
  char *p;
  int imode = 0;
  int number;
  int iregextension=0;
  struct arc_ext_operand_value *ext_oper;
  symbolS *symbolP;

  segT old_sec;
  int old_subsec;

  name = input_line_pointer;
  c = get_symbol_end ();
  name = xstrdup (name);

  p = name;
  while (*p)
    {
      *p = TOLOWER (*p);
      p++;
    }

  /* just after name is now '\0'  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after operand name");
      ignore_rest_of_line ();
      free (name);
      return;
    }

  input_line_pointer++;		/* skip ','  */
  number = get_absolute_expression ();
  /* AUX register can have negative value to fit in the s12
     constraint. */
  if ((number < 0) && (opertype != 2))
    {
      as_bad ("negative operand number %d", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  if (opertype)
    {
      SKIP_WHITESPACE ();

      if (*input_line_pointer != ',')
	{
	  as_bad ("expected comma after register-number");
	  ignore_rest_of_line ();
	  free (name);
	  return;
	}

      input_line_pointer++;		/* skip ','  */
      mode = input_line_pointer;

      if (!strncmp (mode, "r|w", 3) || !strncmp (mode, "w|r",3))
	{
	  imode = 0;
	  input_line_pointer += 3;
	}
      else
	{
	  if (!strncmp (mode, "r", 1))
	    {
	      imode = ARC_REGISTER_READONLY;
	      input_line_pointer += 1;
	    }
	  else
	    {
	      if (strncmp (mode, "w", 1))
		{
		  as_bad ("invalid mode");
		  ignore_rest_of_line ();
		  free (name);
		  return;
		}
	      else
		{
		  imode = ARC_REGISTER_WRITEONLY;
		  input_line_pointer += 1;
		}
	    }
	}
      SKIP_WHITESPACE ();
      if (1 == opertype)
	{
	  if (*input_line_pointer != ',')
	    {
	      as_bad ("expected comma after register-mode");
	      ignore_rest_of_line ();
	      free (name);
	      return;
	    }

	  input_line_pointer++;		/* skip ','  */

	  if (!strncasecmp (input_line_pointer, "cannot_shortcut", 15))
	    {
	      imode |= arc_get_noshortcut_flag ();
	      input_line_pointer += 15;
	    }
	  else
	    {
	      if (strncasecmp (input_line_pointer, "can_shortcut", 12))
		{
		  as_bad ("shortcut designator invalid");
		  ignore_rest_of_line ();
		  free (name);
		  return;
		}
	      else
		{
		  input_line_pointer += 12;
		}
	    }

	  if (*input_line_pointer == ',' )
	    {
	      input_line_pointer++;  /* skip ',' */

	      if  (!strncasecmp (input_line_pointer, "VECTOR", 6))
		{
		  imode |= ARC_REGISTER_SIMD_VR;
		  input_line_pointer +=6;
		  iregextension = 'v';
		}
	      else if (!strncasecmp (input_line_pointer, "SCALAR", 6))
		{
		  imode |= ARC_REGISTER_SIMD_I;
		  input_line_pointer +=6;
		  iregextension = 'i';
		}
	      else if (!strncasecmp (input_line_pointer, "KSCALAR", 7))
		{
		  imode |= ARC_REGISTER_SIMD_K;
		  input_line_pointer +=7;
		  iregextension = 'k';
		}
	      else if (!strncasecmp (input_line_pointer, "DMA", 3))
		{
		  imode |= ARC_REGISTER_SIMD_DR;
		  input_line_pointer +=3;
		  iregextension = 'd';
		}
	      else if (!strncasecmp (input_line_pointer, "CORE", 4))
		  {
		  input_line_pointer +=4;
		  iregextension = 0;
		  }
	      else
		{
		  as_bad ("invalid register class");
		  ignore_rest_of_line ();
		  free (name);
		  return;
		}
	    }
	}
    }

  if (((opertype == 1) && number > 60)
      && (!(imode & ARC_REGISTER_SIMD_DR))
      && (!(imode & ARC_REGISTER_SIMD_VR)))
    {
      as_bad ("core register value (%d) too large", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  if ((opertype == 0) && number > 31)
    {
      as_bad ("condition code value (%d) too large", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  ext_oper = xmalloc (sizeof (struct arc_ext_operand_value));

/* allow extension to be loaded to */
  if (imode != ARC_REGISTER_READONLY)
    arc_ld_ext_mask |= (1 << (number - 32));

  if (opertype)
    {
      /* If the symbol already exists, point it at the new definition.  */
      if ((symbolP = symbol_find (name)))
	{
	  if (S_GET_SEGMENT (symbolP) == reg_section)
	    S_SET_VALUE (symbolP, (valueT) &ext_oper->operand);
	  else
	    {
	      as_bad ("attempt to override symbol: %s", name);
	      ignore_rest_of_line ();
	      free (name);
	      free (ext_oper);
	      return;
	    }
	}
      else
	{
	  /* If its not there, add it.  */
//	  symbol_table_insert (symbol_create (name, reg_section,
//					      (valueT) &ext_oper->operand,
//					      &zero_address_frag));
	}
    }

  ext_oper->operand.name  = name;
  ext_oper->operand.value = number;
  ext_oper->operand.type  = arc_operand_type (opertype);
  ext_oper->operand.flags = imode;

  ext_oper->next = arc_ext_operands;
  arc_ext_operands = ext_oper;

  /* OK, now that we know what this operand is, put a description in
     the arc extension section of the output file.  */

  old_sec    = now_seg;
  old_subsec = now_subseg;



  switch (opertype)
    {
    case 0:
	arc_set_ext_seg (EXT_COND_CODE, number, 0, 0);
	p = frag_more (1);
	*p = 3 + strlen (name) + 1;
	p = frag_more (1);
	*p = EXT_COND_CODE;
	p = frag_more (1);
	*p = number;
	p = frag_more (strlen (name) + 1);
	strcpy (p, name);
      break;
    case 1:
	/* use extended format for vector registers */
	if(imode & (ARC_REGISTER_SIMD_VR | ARC_REGISTER_SIMD_I |
		    ARC_REGISTER_SIMD_K  | ARC_REGISTER_SIMD_DR)){
	    arc_set_ext_seg(EXT_LONG_CORE_REGISTER, number,iregextension,0);
	    p = frag_more(1);
	    *p = 8+strlen(name)+1;
	    p = frag_more(1);
	    *p = 9;
	    p = frag_more(1);
	    *p = number;
	    p = frag_more(1);/* first flags byte*/
	    *p = 0;
	    p = frag_more(1);/* second flags byte*/
	    *p = 0;
	    p = frag_more(1);/* third flags byte*/
	    *p = 0;
	    p = frag_more(1);/* fourth flags byte*/
	    *p = REG_WRITE | REG_READ;
	    if(imode & ARC_REGISTER_WRITEONLY)
		*p = REG_WRITE;
	    if(imode & ARC_REGISTER_READONLY)
		*p = REG_READ;
	    p = frag_more(1);
	    *p = 'v';
	    if(imode & ARC_REGISTER_SIMD_VR)
		*p = 'v';
	    if(imode & ARC_REGISTER_SIMD_I)
		*p = 'i';
	    if(imode & ARC_REGISTER_SIMD_K)
		*p = 'k';
	    if(imode & ARC_REGISTER_SIMD_DR)
		*p = 'd';
	    p = frag_more (strlen(name) + 1);
	    strcpy(p,name);
	    break;
	    }
       /* use old format for all others */
      arc_set_ext_seg (EXT_CORE_REGISTER, number, iregextension, 0);
      if (imode & (ARC_REGISTER_WRITEONLY | ARC_REGISTER_READONLY))
	{
	  p = frag_more (1);
	  *p = 7 + strlen (name) + 1;
	  p = frag_more (1);
	  *p = EXT_LONG_CORE_REGISTER;
	  p = frag_more (1);
	  *p = number;

	  p = frag_more (3);
	  *(p + 0) = 0;
	  *(p + 1) = 0;
	  *(p + 2) = 0;

	  p = frag_more (1);
	  *p = (imode == ARC_REGISTER_WRITEONLY) ? REG_WRITE : REG_READ;

	  p = frag_more (strlen (name) + 1);
	  strcpy (p, name);
	}
      else
	{
	  p = frag_more (1);
	  *p = 3 + strlen (name) + 1;
	  p = frag_more (1);
	  *p = EXT_CORE_REGISTER;
	  p = frag_more (1);
	  *p = number;
	  p = frag_more (strlen (name) + 1);
	  strcpy (p, name);
	}
      break;
    case 2:
      arc_set_ext_seg (EXT_AUX_REGISTER, number, 0, 0);
      p = frag_more (1);
      *p = 6 + strlen (name) + 1;
      p = frag_more (1);
      *p = EXT_AUX_REGISTER;
      p = frag_more (1);
      *p = number >> 24 & 0xff;
      p = frag_more (1);
      *p = number >> 16 & 0xff;
      p = frag_more (1);
      *p = number >>  8 & 0xff;
      p = frag_more (1);
      *p = number       & 0xff;
      p = frag_more (strlen (name) + 1);
      strcpy (p, name);
      break;
    default:
      as_bad ("invalid opertype");
      ignore_rest_of_line ();
      free (name);
      return;
      break;
    }

  subseg_set (old_sec, old_subsec);

  /* Enter all registers into the symbol table.  */

  demand_empty_rest_of_line ();
}

/*********************************************************************/
/* Here's all the ARCompact extension instruction assembling stuff.  */
/*********************************************************************/

/* Lots of the code here needs to used for the ARCTangent .extinstruction
   directive too.  For now, both the code exists but leisurely cleanup would
   eliminate a lot of the old code.  */

/* This structure will replace suffixclass and syntaxclass.  */

struct attributes {
  char *name;
  int len;
  int class;
  };

static const struct attributes ac_suffixclass[] = {
  { "SUFFIX_FLAG", 11, AC_SUFFIX_FLAG},
  { "SUFFIX_COND", 11, AC_SUFFIX_COND},
  /* START ARC LOCAL */
  { "SUFFIX_DIRECT", 13, AC_SUFFIX_DIRECT},
  /* END ARC LOCAL */
  { "SUFFIX_NONE", 11, AC_SUFFIX_NONE},
  { "FLAGS_NONE" , 10, AC_SIMD_FLAGS_NONE},
  { "FLAG_SET"   ,  8, AC_SIMD_FLAG_SET},
  { "FLAG1_SET"  ,  9, AC_SIMD_FLAG1_SET},
  { "FLAG2_SET"  ,  9, AC_SIMD_FLAG2_SET},
  { "ENCODE_U8"  ,  9, AC_SIMD_ENCODE_U8},
  { "ENCODE_U6"  ,  9, AC_SIMD_ENCODE_U6},
  { "SCALE_0"    ,  7, AC_SIMD_SCALE_0},
  { "SCALE_1"    ,  7, AC_SIMD_SCALE_1},
  { "SCALE_2"    ,  7, AC_SIMD_SCALE_2},
  { "SCALE_3"    ,  7, AC_SIMD_SCALE_3},
  { "SCALE_4"    ,  7, AC_SIMD_SCALE_4},
  { "ENCODE_LIMM", 11, AC_SIMD_ENCODE_LIMM},
  { "EXTENDED_MULTIPLY", 17, AC_EXTENDED_MULTIPLY},
  { "EXTENDED",     8, AC_SIMD_EXTENDED},
  { "EXTEND2",      7, AC_SIMD_EXTEND2},
  { "EXTEND3",      7, AC_SIMD_EXTEND3},
  { "SUFFIX_MASK", 11, AC_SUFFIX_LANEMASK},
  { "ENCODE_S12",  10, AC_SIMD_ENCODE_S12},
  { "ENCODE_ZEROA",12, AC_SIMD_ZERVA},
  { "ENCODE_ZEROB",12, AC_SIMD_ZERVB},
  { "ENCODE_ZEROC",12, AC_SIMD_ZERVC},
  { "ENCODE_SETLM",12, AC_SIMD_SETLM},
  { "EXTEND1",      7, AC_SIMD_EXTEND1},
  { "ENCODE_KREG", 11, AC_SIMD_KREG},
  { "ENCODE_U16",  10, AC_SIMD_ENCODE_U16},
  { "ENCODE_ZR",    9, AC_SIMD_ENCODE_ZR}
};

#define AC_MAXSUFFIXCLASS (sizeof (ac_suffixclass) / sizeof (struct attributes))

/* remember, if any entries contain other entries as prefixes, the longer
 * entries must be first.
 */
static const struct attributes ac_syntaxclass[] = {
  { "SYNTAX_3OP", 10, AC_SYNTAX_3OP},
  { "SYNTAX_2OP", 10, AC_SYNTAX_2OP},
  { "SYNTAX_1OP", 10, AC_SYNTAX_1OP},
  { "SYNTAX_NOP", 10, AC_SYNTAX_NOP},
  { "SYNTAX_VbI0"     , 11, AC_SIMD_SYNTAX_VbI0},
  { "SYNTAX_Vb00"     , 11, AC_SIMD_SYNTAX_Vb00},
  { "SYNTAX_VbC0"     , 11, AC_SIMD_SYNTAX_VbC0},
  { "SYNTAX_VVV"      , 10, AC_SIMD_SYNTAX_VVV},
  { "SYNTAX_VV0"      , 10, AC_SIMD_SYNTAX_VV0},
  { "SYNTAX_V00"      , 10, AC_SIMD_SYNTAX_V00},
  { "SYNTAX_VC0"      , 10, AC_SIMD_SYNTAX_VC0},
  { "SYNTAX_VVC"      , 10, AC_SIMD_SYNTAX_VVC},
  { "SYNTAX_VVI"      , 10, AC_SIMD_SYNTAX_VVI},
  { "SYNTAX_VVL"      , 10, AC_SIMD_SYNTAX_VVL},
  { "SYNTAX_VU0"      , 10, AC_SIMD_SYNTAX_VU0},
  { "SYNTAX_VL0"      , 10, AC_SIMD_SYNTAX_VL0},
  { "SYNTAX_C00"      , 10, AC_SIMD_SYNTAX_C00},
  { "SYNTAX_VV"       ,  9, AC_SIMD_SYNTAX_VV},
  { "SYNTAX_CC"       ,  9, AC_SIMD_SYNTAX_CC},
  { "SYNTAX_C0"       ,  9, AC_SIMD_SYNTAX_C0},
  { "SYNTAX_DC"       ,  9, AC_SIMD_SYNTAX_DC},
  { "SYNTAX_D0"       ,  9, AC_SIMD_SYNTAX_D0},
  { "SYNTAX_VD"       ,  9, AC_SIMD_SYNTAX_VD},
  { "SYNTAX_C"        ,  8, AC_SIMD_SYNTAX_C},
  { "SYNTAX_0"        ,  8, AC_SIMD_SYNTAX_0}
};

#define AC_MAXSYNTAXCLASS (sizeof (ac_syntaxclass) / sizeof (struct attributes))

static const struct attributes ac_syntaxclassmodifier[] = {
  { "OP1_DEST_IGNORED", 16, AC_OP1_DEST_IGNORED},
  { "OP1_IMM_IMPLIED" , 15, AC_OP1_IMM_IMPLIED},
  { "OP1_MUST_BE_IMM" , 15, AC_OP1_MUST_BE_IMM},
  { "SYNTAX_DISC"     , 11, AC_SIMD_SYNTAX_DISC},
  { "SYNTAX_IREGA"    , 12, AC_SIMD_IREGA},
  { "SYNTAX_IREGB"    , 12, AC_SIMD_IREGB}
  };

#define AC_MAXSYNTAXCLASSMODIFIER (sizeof (ac_syntaxclassmodifier) / sizeof (struct attributes))

/* This macro takes the various fields of a 32-bit extension instruction and
   builds the instruction word.  */
#define INSN_32(m,i,p,a,b,c)	(((m & 0x1f) << 27) | \
				 ((i & 0x3f) << 16) | \
				 ((p & 0x03) << 22) | \
				 ((a & 0x3f) << 0)  | \
				 ((b & 0x07) << 24) | \
				 ((b & 0x38) << 9)  | \
				 ((c & 0x3f) << 6))

/* This macro takes the various fields of a 16-bit extension instruction and
   builds the instruction word.  */
#define INSN_16(I,b,c,i)	(((I & 0x1f) << 11) | \
				 ((b & 0x07) << 8)  | \
				 ((c & 0x07) << 5)  | \
				 ((i & 0x1f) << 0))

/* This macro plugs in the I-field into a 32-bit instruction.  There are two
   definitions here.  The first one is in accordance with the ARCompact
   Programmer's Reference while the other is what Metaware does and what
   seems to be the more correct thing to do.  */
#ifndef UNMANGLED
#define I_FIELD(x,i)		((( x << 1) & (64 - (1 << i)))  | \
				 ( x & (i - 1))			| \
				 ((x & 0x20) >> (6 - i)))
#else
#define I_FIELD(x,i)		(x & 0x3f)
#endif

/* This function generates the list of extension instructions.  The last
   argument is used to append a .f, .di, or a .cc to the instruction name.  */
static void
arc_add_ext_inst (char *name, char *operands, unsigned long value,
		  unsigned long mask ATTRIBUTE_UNUSED, unsigned flags, unsigned suffix)
{
  char realsyntax[160];
  struct arc_opcode *ext_op;

  ext_op = (struct arc_opcode *) xmalloc (sizeof (struct arc_opcode));

  strcpy (realsyntax,name);

  if(suffix & AC_SUFFIX_COND){
    strcat(realsyntax,"%.q");
      }

  /* START ARC LOCAL */
  if(suffix & AC_SUFFIX_DIRECT){
    strcat(realsyntax,"%.V");
      }
  /* END ARC LOCAL */

  if(suffix & AC_SUFFIX_FLAG){
    strcat(realsyntax,"%.f");
      }

  strcat (realsyntax,operands);

  flags = flags & ~(ARC_SIMD_ZERVA|ARC_SIMD_ZERVB|ARC_SIMD_ZERVC|
		     ARC_SIMD_SETLM);
  if(suffix&AC_SIMD_ZERVA)
      flags |= ARC_SIMD_ZERVA;
  if(suffix&AC_SIMD_ZERVB){
      flags |= ARC_SIMD_ZERVB;
      }
  if(suffix&AC_SIMD_ZERVC)
      flags |= ARC_SIMD_ZERVC;
  if(suffix&AC_SIMD_SETLM)
      flags |= ARC_SIMD_SETLM;

  ext_op->syntax = (unsigned char *) xstrdup (realsyntax);
  ext_op->value = value;
  ext_op->flags = flags | ARCOMPACT ;
  ext_op->next_asm = arc_ext_opcodes;
  ext_op->next_dis = arc_ext_opcodes;
  arc_ext_opcodes = ext_op;
}
/* This function generates the list of extension instructions.  The last
   argument is used to append a .f or a .cc to the instruction name.  */
static void
arc_add_long_ext_inst (char *name, char *operands, unsigned long value,
		  unsigned long mask ATTRIBUTE_UNUSED, unsigned long value2,
		       unsigned long mask2 ATTRIBUTE_UNUSED,
		       unsigned long flags, unsigned long suffix)
{
  char realsyntax[160];
  struct arc_opcode *ext_op;

  ext_op = (struct arc_opcode *) xmalloc (sizeof (struct arc_opcode));

  strcpy (realsyntax,name);

  if(suffix & AC_SUFFIX_COND){
    strcat(realsyntax,"%.q");
      }

  if(flags & ARC_SIMD_LANEMASK){
      strcat(realsyntax,"%.]");
      }

  if(suffix & AC_SUFFIX_FLAG){
    strcat(realsyntax,"%.f");
      }

  strcat (realsyntax,operands);

  flags = flags & ~(ARC_SIMD_ZERVA | ARC_SIMD_ZERVB | ARC_SIMD_ZERVC|
		     ARC_SIMD_SETLM);
  if(suffix & AC_SIMD_ZERVA)
      flags |= ARC_SIMD_ZERVA;
  if(suffix & AC_SIMD_ZERVB){
      flags |= ARC_SIMD_ZERVB;
      }
  if(suffix & AC_SIMD_ZERVC)
      flags |= ARC_SIMD_ZERVC;
  if(suffix & AC_SIMD_SETLM)
      flags |= ARC_SIMD_SETLM;

  ext_op->syntax = (unsigned char *) xstrdup (realsyntax);
  ext_op->value = value;
  ext_op->value2 = value2;
  ext_op->flags = flags | ARCOMPACT | SIMD_LONG_INST;
  ext_op->next_asm = arc_ext_opcodes;
  ext_op->next_dis = arc_ext_opcodes;
  arc_ext_opcodes = ext_op;
}

/* This function generates the operand strings based on the syntax class and
 *  syntax class modifiers and does some error checking.
 * instruction name      name of instruction.
 * major-opcode          five bit major opcode.
 * sub_opcode            sub operation code. If long simd we also pack in
 *                         a mode and two other sub op code.
 *                       a3322ss
 *                       a is accumulation mode in second word of long form.
 *                       3 is third op-code which goes in operand b
 *                       2 is second op-code which goes in operand a
 *                       s is sub op-code in sub op-code field.
 * syntax_class          or'd syntax class flags.
 * syntax_class_modifiers  or'd syntax modifier flags.
 * suffix_class          or'd suffix class flags.
 * returns               number of operands.
 */
static int
arc_generate_extinst32_operand_strings (char *instruction_name,
					unsigned char major_opcode,
					unsigned long sub_opcode,
					unsigned long syntax_class,
					unsigned long syntax_class_modifiers,
					unsigned long suffix_class)
{
  char op1[6], op2[6], op3[6], operand_string[18];
  unsigned long xmitsuffix;
  /* char suffixstr[10]; */
  int nop = 0;
  int i;
  unsigned long insn,mask,insn2,mask2;
  /* The ARCompact reference manual states this range to be 0x04 to 0x07
     but this is the correct thing.  */
  if((major_opcode > 0x0a) || (major_opcode < 0x04))
    {
      as_bad ("major opcode not in range [0x04-0x0a]");
      ignore_rest_of_line ();
      return 0;
    }

  if(sub_opcode > 0x3f&&major_opcode!=0x0a&&major_opcode!=5&&major_opcode!=9)
    {
      as_bad ("sub opcode not in range [0x00-0x3f]");
      ignore_rest_of_line ();
      return 0;
    }
  switch(syntax_class &
	 (AC_SYNTAX_3OP | AC_SYNTAX_2OP | AC_SYNTAX_1OP | AC_SYNTAX_NOP
	  | AC_SYNTAX_SIMD))
    {
    case AC_SYNTAX_3OP:

      if(suffix_class & AC_SUFFIX_COND)
	{
	  arc_add_ext_inst (instruction_name, " 0,%L,%L%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 0, 62, 62),
			    INSN_32(-1,-1,-1,32,-1,-1),
			    syntax_class | syntax_class_modifiers,
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	  arc_add_ext_inst (instruction_name, " 0,%L,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 32, 62, 0),
			    INSN_32(-1,-1,-1,32,-1,0),
			    syntax_class | syntax_class_modifiers,
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	  arc_add_ext_inst (instruction_name, " 0,%L,%C%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 0, 62, 0),
			    INSN_32(-1,-1,-1,32,-1,0),
			    syntax_class | syntax_class_modifiers,
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	  if(!(syntax_class_modifiers & AC_OP1_MUST_BE_IMM))
	    {
	      arc_add_ext_inst (instruction_name, "%Q %#,%B,%L%F",
				INSN_32(major_opcode,
					I_FIELD(sub_opcode, 1),
					3, 0, 0, 62),
				INSN_32(-1,-1,-1,0,0,0),
				(syntax_class | syntax_class_modifiers),
				suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	      arc_add_ext_inst (instruction_name, " %#,%B,%u%F",
				INSN_32(major_opcode,
					I_FIELD(sub_opcode, 0),
					3, 32, 0, 0),
				INSN_32(-1,-1,-1,32,0,0),
				(syntax_class | syntax_class_modifiers),
				suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	      arc_add_ext_inst (instruction_name, " %#,%B,%C%F",
				INSN_32(major_opcode,
					I_FIELD(sub_opcode, 1),
					3, 0, 0, 0),
				INSN_32(-1,-1,-1,0,0,0),
				(syntax_class | syntax_class_modifiers),
				suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));
	    }
	}
      if(suffix_class & AC_EXTENDED_MULTIPLY)
	  {
	  arc_add_ext_inst (instruction_name, " 0,%B,%C%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 0, 0, 0),
			    INSN_32(-1, -1, -1, 32, 0, 0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));
	  arc_add_ext_inst (instruction_name, " 0,%B,%L%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 0, 0, 62),
			    INSN_32(-1, -1 , -1, 32, 0, -1),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));
	  arc_add_ext_inst (instruction_name, " 0,%B,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    3, 32, 0, 0),
			    INSN_32(-1, -1, -1, 32, 0, 0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));
	  }

      if(!(syntax_class_modifiers & AC_OP1_MUST_BE_IMM))
	{

	  arc_add_ext_inst (instruction_name, "%Q %A,%B,%L%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 0, 0, 62),
			    INSN_32(-1,-1,-1,0,0,-1),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %#,%B,%K%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    2, 0, 0, 0),
			    INSN_32(-1,-1,-1,0,0,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %A,%B,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 2),
				    1, 0, 0, 0),
			    INSN_32(-1,-1,-1,0,0,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %A,%B,%C%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 0, 0, 0),
			    INSN_32(-1,-1,-1,0,0,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, "%Q %A,%L,%L%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 0, 62, 62),
			    INSN_32(-1,-1,-1,0,-1,-1),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, "%Q %A,%L,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 2),
				    1, 0, 62, 0),
			    INSN_32(-1,-1,-1,0,-1,-1),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, "%Q %A,%L,%C%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 0, 62, 0),
			    INSN_32(-1,-1,-1,0,-1,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));
	}

      arc_add_ext_inst (instruction_name, "%Q 0,%L,%L%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 1),
				0, 62, 62, 62),
			INSN_32(-1,-1,-1,-1,-1,-1),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q 0,%L,%K%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 1),
				2, 0, 62, 0),
			INSN_32(-1,-1,-1,0,-1,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q 0,%L,%u%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 2),
				1, 62, 62, 0),
			INSN_32(-1,-1,-1,-1,-1,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%L,%C%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 1),
				0, 62, 62, 0),
			INSN_32(-1,-1,-1,0,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q 0,%B,%L%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 1),
				0, 62, 0, 62),
			INSN_32(-1,-1,-1,-1,0,-1),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%B,%u%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 2),
				1, 62, 0, 0),
			INSN_32(-1,-1,-1,0,-1,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%B,%K%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 2),
				1, 62, 0, 0),
			INSN_32(-1,-1,-1,0,-1,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%B,%C%F",
			INSN_32(major_opcode,
				I_FIELD(sub_opcode, 1),
				0, 62, 0, 0),
			INSN_32(-1,-1,-1,0,-1,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));
      break;

    case AC_SYNTAX_2OP:
      if (sub_opcode == 0x3f)
	{
	  as_bad ("Subopcode 0x3f not allowed with SYNTAX_2OP\n");
	}

      arc_add_ext_inst (instruction_name, "%Q %L,%u%F",
			INSN_32(major_opcode,
				0x2f,
				1, sub_opcode, 62, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q %L,%C%F",
			INSN_32(major_opcode,
				0x2f,
				0, sub_opcode, 62, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q %B,%L%F",
			INSN_32(major_opcode,
				0x2f,
				0, sub_opcode, 0, 62),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " %B,%u%F",
			INSN_32(major_opcode,
				0x2f,
				1, sub_opcode, 0, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " %B,%C%F",
			INSN_32(major_opcode,
				0x2f,
				0, sub_opcode, 0, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, "%Q 0,%L%F",
			INSN_32(major_opcode,
				0x2f,
				0, sub_opcode, 62, 62),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%u%F",
			INSN_32(major_opcode,
				0x2f,
				1, sub_opcode, 62, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " 0,%C%F",
			INSN_32(major_opcode,
				0x2f,
				0, sub_opcode, 62, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      /* START ARC LOCAL */
      if(suffix_class & AC_SUFFIX_DIRECT)
      {
	 arc_add_ext_inst (instruction_name, " %#,[%C]",
			   INSN_32(major_opcode,
				   0x2f,
				   0, sub_opcode, 0, 0),
			   INSN_32(-1, -1, -1, -1, 0, 0),
			   (syntax_class | syntax_class_modifiers),
			   syntax_class & (AC_SUFFIX_DIRECT));
	  arc_add_ext_inst (instruction_name, " %#,[%L]",
			    INSN_32(major_opcode,
				    0x2f,
				    0, sub_opcode, 0, 62),
			    INSN_32(-1, -1, -1, -1, 0, -1),
			    (syntax_class | syntax_class_modifiers),
			    syntax_class & (AC_SUFFIX_DIRECT));
	  arc_add_ext_inst (instruction_name, " %#,[%u]",
			    INSN_32(major_opcode,
				    0x2f,
				    1, sub_opcode, 0, 62),
			    INSN_32(-1, -1, -1, -1, 0, -1),
			    (syntax_class | syntax_class_modifiers),
			    syntax_class & (AC_SUFFIX_DIRECT));
	  arc_add_ext_inst (instruction_name, " 0,[%C]",
			    INSN_32(major_opcode,
				    0x2f,
				    0, sub_opcode, 62, 0),
			    INSN_32(-1, -1, -1, -1, -1, 0),
			    (syntax_class | syntax_class_modifiers),
			    syntax_class & (AC_SUFFIX_DIRECT));
	  arc_add_ext_inst (instruction_name, " 0,[%L]",
			    INSN_32(major_opcode,
				    0x2f,
				    0, sub_opcode, 62, 62),
			    INSN_32(-1, -1, -1, -1, -1, 0),
			    (syntax_class | syntax_class_modifiers),
			    syntax_class & (AC_SUFFIX_DIRECT));
	  arc_add_ext_inst (instruction_name, " 0,[%u]",
			    INSN_32(major_opcode,
				    0x2f,
				    1, sub_opcode, 62, 62),
			    INSN_32(-1, -1, -1, -1, -1, 0),
			    (syntax_class | syntax_class_modifiers),
			    syntax_class & (AC_SUFFIX_DIRECT));
      }
      /* END ARC LOCAL */

      if (syntax_class_modifiers & AC_OP1_IMM_IMPLIED)
      {

	  if(suffix_class & AC_SUFFIX_COND)
	  {
	      arc_add_ext_inst (instruction_name, "%Q %L,%C%F",
				INSN_32(major_opcode,
					I_FIELD(sub_opcode, 1),
					3, 0, 62, 0),
				INSN_32(-1,-1,-1,32,-1,0),
				syntax_class | syntax_class_modifiers,
				suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));

	      arc_add_ext_inst (instruction_name, "%Q %L,%u%F",
				INSN_32(major_opcode,
					I_FIELD(sub_opcode, 1),
					3, 32, 62, 0),
				INSN_32(-1,-1,-1,32,-1,0),
				syntax_class | syntax_class_modifiers,
				suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND));
	  }

	  arc_add_ext_inst (instruction_name, "%Q %L,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 2),
				    1, 62, 62, 0),
			    INSN_32(-1,-1,-1,-1,-1,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, "%Q %B,%L%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 62, 0, 62),
			    INSN_32(-1,-1,-1,-1,0,-1),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %B,%u%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 2),
				    1, 62, 0, 0),
			    INSN_32(-1,-1,-1,0,-1,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %B,%K%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 2),
				    1, 62, 0, 0),
			    INSN_32(-1,-1,-1,0,-1,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));

	  arc_add_ext_inst (instruction_name, " %B,%C%F",
			    INSN_32(major_opcode,
				    I_FIELD(sub_opcode, 1),
				    0, 62, 0, 0),
			    INSN_32(-1,-1,-1,0,-1,0),
			    (syntax_class | syntax_class_modifiers),
			    suffix_class & (AC_SUFFIX_FLAG));
      }

      break;

    case AC_SYNTAX_1OP:

      arc_add_ext_inst (instruction_name, "%Q %L%F",
			INSN_32(major_opcode,
				0x2f,
				0, 0x3f, sub_opcode, 62),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " %u%F",
			INSN_32(major_opcode,
				0x2f,
				1, 0x3f, sub_opcode, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));

      arc_add_ext_inst (instruction_name, " %C%F",
			INSN_32(major_opcode,
				0x2f,
				0, 0x3f, sub_opcode, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));
      break;

    case AC_SYNTAX_NOP:

      /* FIXME: The P field need not be 1 necessarily. The value to be
       * plugged in will depend on the final ABI statement for the same */
      arc_add_ext_inst (instruction_name, "%F",
			INSN_32(major_opcode,
				0x2f,
				1, 0x3f, sub_opcode, 0),
			INSN_32(-1,-1,-1,-1,0,0),
			(syntax_class | syntax_class_modifiers),
			suffix_class & (AC_SUFFIX_FLAG));
      break;
    case AC_SYNTAX_SIMD:
      op1[0] = op2 [0] = op3[0] = operand_string[0] = '\0';
      nop = 0;
      /* suffixstr[0] = '\0'; */
      use_extended_instruction_format = 1;
      for(i = 0; i < RCLASS_SET_SIZE; i++)
	  extended_register_format[i] = 0;
      for(i = 0; i < OPD_FORMAT_SIZE; i++){
	  extended_operand_format1[i] = 0;
	  extended_operand_format2[i] = 0;
	  extended_operand_format3[i] = 0;
	  }
      for(i = 0; i < 4; i++)
	  extended_instruction_flags[i] = 0;
      switch (syntax_class
	      & (AC_SIMD_SYNTAX_VVV | AC_SIMD_SYNTAX_VV | AC_SIMD_SYNTAX_VV0
		 | AC_SIMD_SYNTAX_VbI0 | AC_SIMD_SYNTAX_Vb00
		 | AC_SIMD_SYNTAX_V00 | AC_SIMD_SYNTAX_0 |AC_SIMD_SYNTAX_C00
		 | AC_SIMD_SYNTAX_C0  | AC_SIMD_SYNTAX_D0 | AC_SIMD_SYNTAX_VD))
	{
	case AC_SIMD_SYNTAX_VVV:
	  extended_register_format[0] = 'v';
	  extended_register_format[1] = 'v';
	  extended_register_format[2] = 'v';
	  strcpy (op1, " %*,");
	  if(syntax_class_modifiers & AC_SIMD_IREGA){
	      if(suffix_class & AC_SIMD_KREG){
		  extended_register_format[0] = 'k';
		  strcpy(op1," %\15,");}
	      else {
		  extended_register_format[0] = 'i';
		  strcpy(op1," %\13,");}
	      }
	  if(suffix_class & AC_SIMD_ZERVA)
	      strcpy(op1," %\23,");
	  strcpy (op2, "%(,");
	  strcpy (op3, "%)");
	  if(syntax_class & AC_SIMD_SYNTAX_VVI){
	      if(suffix_class & AC_SIMD_KREG){
		  extended_register_format[2] = 'k';
		  strcpy(op3, "%\17");}
	      else {
		  extended_register_format[2] = 'i';
		  strcpy(op3, "%}");}
	      }
	  if(syntax_class & AC_SIMD_SYNTAX_VVL){
	      extended_register_format[2] = '0';
	      strcpy(op3,"%\24");
	      suffix_class |= AC_SIMD_ZERVC;
	      }
	  nop = 3;
	  break;
	case AC_SIMD_SYNTAX_VV:
	  strcpy (op1, " %(");
	  extended_register_format[0] = 'v';
	  extended_register_format[1] = 'v';
	  if(suffix_class & AC_SIMD_EXTEND3){
	      strcpy (op1, " %)");
	      nop=1;
	      break;
	      }
	  if(suffix_class & AC_SIMD_ZERVB)
	      strcpy (op1, " %\23");
	  strcpy (op2, ",%)");
	  if(syntax_class & AC_SIMD_SYNTAX_VVL){
	      extended_register_format[1] = '0';
	      strcpy(op2, ",%\24");
	      suffix_class |= AC_SIMD_ZERVC;
	      }
	  nop = 2;
	  break;
	case AC_SIMD_SYNTAX_VV0:
	    extended_register_format[0] = 'v';
	    extended_register_format[1] = 'v';
	    if(suffix_class & AC_SIMD_EXTEND2){
		strcpy(op1," %(,");
		if(syntax_class_modifiers & AC_SIMD_IREGB){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[0] = 'k';
			strcpy(op1," %\16,");}
		    else {
			extended_register_format[0] = 'i';
			strcpy(op1," %{,");}
		    }
		if(suffix_class & AC_SIMD_ZERVB)
		    strcpy(op1," %\23,");
		strcpy(op2,"%)");
		if(syntax_class & AC_SIMD_SYNTAX_VVI){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[1] = 'k';
			strcpy(op2,"%\17");}
		    else {
			extended_register_format[1] = 'i';
			strcpy(op2,"%}");}
		    }
		if(suffix_class & AC_SIMD_ENCODE_U6){
		    extended_register_format[1] = '0';
		    strcpy(op2,"%u");
		    }
		if(suffix_class & AC_SIMD_ENCODE_U16){
		    extended_register_format[1] = '0';
		    strcpy(op2,"%\20");
		    }
		if(suffix_class & AC_SIMD_ENCODE_U8){
		    extended_register_format[1] = '0';
		    strcpy(op2,"%?");
		    }
		if(syntax_class & AC_SIMD_SYNTAX_VVL){
		    extended_register_format[1] = '0';
		    strcpy(op2,"%\24");
		    suffix_class |= AC_SIMD_ZERVC;
		    }
		if(syntax_class & AC_SIMD_SYNTAX_VVC){
		    extended_register_format[1] = 'c';
		    strcpy(op2,"%C");
		    }
		nop = 2;
		break;
		}
	    else
		{
		strcpy (op1, " %*,");
		if(syntax_class_modifiers & AC_SIMD_IREGA){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[0] = 'k';
			strcpy(op1," %\15,");}
		    else {
			extended_register_format[0] = 'i';
			strcpy(op1," %\13,");}
		    }
		if(suffix_class & AC_SIMD_ZERVA)
		    strcpy(op1, " %\23,");
		strcpy (op2, "%(,");
		strcpy(op3,"");
		switch  (syntax_class
			 & (AC_SIMD_SYNTAX_VVC | AC_SIMD_SYNTAX_VVI | AC_SIMD_SYNTAX_VVL))
		    {
		    case AC_SIMD_SYNTAX_VVC:
			extended_register_format[2] = 'c';
			strcpy (op3, "%C");
			break;
		    case AC_SIMD_SYNTAX_VVI:
			if(suffix_class & AC_SIMD_KREG){
			    extended_register_format[2] = 'k';
			    strcpy (op3, "%\17");}
			else {
			    extended_register_format[2] = 'i';
			    strcpy (op3, "%}");}
			break;
		    case AC_SIMD_SYNTAX_VVL:
			extended_register_format[2] = '0';
			strcpy(op3,"%L");
			if(suffix_class & AC_SIMD_EXTENDED)
			    strcpy(op3,"%\24");
			suffix_class |= AC_SIMD_ZERVC;
			break;
		    default:
			if (suffix_class & AC_SIMD_ENCODE_U8){
			    extended_register_format[2] = '0';
			    strcpy (op3, "%?");}
			else if ( suffix_class & AC_SIMD_ENCODE_U6) {
			    extended_register_format[2] = '0';
			    strcpy (op3, "%u"); }
			else if ( suffix_class & AC_SIMD_ENCODE_U16) {
			    extended_register_format[2] = '0';
			    strcpy (op3, "%\20");}
			break;
		    }
		if(strcmp(op3,"")!=0)
		    nop = 3;
		else
		    nop = 2;
		}
	  break;

	case AC_SIMD_SYNTAX_VbI0:
	  extended_register_format[0] = 'v';
	  extended_register_format[1] = 'i';
	  extended_register_format[2] = '0';
	  strcpy (op1, " %*,");
	  if(suffix_class & AC_SIMD_KREG){
	      extended_register_format[1] = 'k';
	      strcpy (op2, "%\16,");}
	  else
	      strcpy (op2, "%{,");
	  if (suffix_class & AC_SIMD_ENCODE_U6)
	    strcpy (op3, "%u");
	  if (suffix_class & AC_SIMD_ENCODE_ZR)
	    strcpy (op3, "%\25");
	  if (suffix_class & AC_SIMD_ENCODE_U8)
	    strcpy (op3, "%?");
	  if (suffix_class & AC_SIMD_ENCODE_S12)
	    strcpy (op3, "%\14");
	  nop = 3;
	  if (!strcmp(op3,""))//temp .. please remove
	    printf("SYNTAX_VbI0 op3 not found:%s\n",instruction_name);
	  break;

	case AC_SIMD_SYNTAX_Vb00:
	  extended_register_format[0] = 'v';
	  extended_register_format[0] = 'i';
	  extended_register_format[0] = '0';
	  strcpy (op1, " %*,");
	  if(suffix_class & AC_SIMD_KREG){
	      extended_register_format[1] = 'k';
	      strcpy(op2,"%\16");}
	  else
	      strcpy(op2,"%{");
	  if (syntax_class & AC_SIMD_SYNTAX_VbC0){
	      extended_register_format[1] = 'c';
	      strcpy (op2, "%B");
	      }
	  if (suffix_class & AC_SIMD_ENCODE_ZR)
	    strcpy (op3, ",%\25");
	  if (suffix_class & AC_SIMD_ENCODE_U8)
	    strcpy (op3, ",%?");
	  if (suffix_class & AC_SIMD_ENCODE_S12)
	    strcpy (op3, ",%\14");
	  if(strcmp(op3,"")!=0)
	      nop =3;
	  else {
	      extended_register_format[2] = 0;
	      nop = 2;
	      }
	  break;

	case AC_SIMD_SYNTAX_V00:
	    extended_register_format[0] = 'v';
	    extended_register_format[1] = '0';
	    if((suffix_class & AC_SIMD_EXTEND2) || (suffix_class & AC_SIMD_EXTEND3)){
		strcpy(op1," %(,");
		if(suffix_class & AC_SIMD_ZERVB){
		    strcpy(op1," %\23,");
		    }
		strcpy(op2,"%u");
		if(syntax_class_modifiers & AC_SIMD_IREGA){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[1] = 'k';
			strcpy(op2,"%\17");}
		    else {
			extended_register_format[1] = 'i';
			strcpy(op2,"%}");}
		    }
		if(syntax_class&AC_SIMD_SYNTAX_VC0)
		    strcpy(op2,"%C");
		if(suffix_class & AC_SIMD_ENCODE_LIMM){
		    strcpy(op2,"%L");
		    suffix_class |= AC_SIMD_ZERVC;
		    }
		if(syntax_class & AC_SIMD_SYNTAX_VL0){
		    suffix_class |= AC_SIMD_ZERVC;
		    strcpy(op2,"%L");
		    }
		if(suffix_class & AC_SIMD_ENCODE_S12)
		    strcpy(op2,"%\14");
		nop = 2;
		}
	    else
		{
		strcpy (op1, " %*,");
		if(syntax_class_modifiers & AC_SIMD_IREGA){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[0] = 'k';
			strcpy(op1, " %\15,");}
		    else {
			extended_register_format[0] = 'i';
			strcpy(op1, " %\13,");}
		    }
		if(suffix_class & AC_SIMD_ZERVA){
		    strcpy(op1," %\23,");
		    }

		extended_register_format[2] = '0';
		if (suffix_class & AC_SIMD_ENCODE_U8)
		    strcpy (op3, "%?");
		if(suffix_class & AC_SIMD_ENCODE_U6)
		    strcpy(op3, "%u");
		if(suffix_class & AC_SIMD_ENCODE_U16)
		    strcpy(op3, "%\20");
		if(suffix_class & AC_SIMD_ENCODE_LIMM){
		    suffix_class |= AC_SIMD_ZERVC;
		    strcpy(op3, "%L");
		}
		if(syntax_class & AC_SIMD_SYNTAX_VVC){
		    extended_register_format[1] = 'c';
		    strcpy(op3, "%C");
		    }
		if(suffix_class & AC_SIMD_ENCODE_S12)
		    strcpy(op3,"%\14");
		if(strcmp(op3,"")==0){
		    extended_register_format[1] = '0';
		    if (syntax_class & AC_SIMD_SYNTAX_VC0){
			extended_register_format[1] = 'c';
			strcpy (op2, "%B");
			}
		    if(syntax_class & AC_SIMD_SYNTAX_VU0)
			strcpy(op2, "%u");
		    if(syntax_class & AC_SIMD_SYNTAX_VL0){
			suffix_class |= AC_SIMD_ZERVC;
			strcpy(op2, "%L");
		     }
		    if(syntax_class_modifiers & AC_SIMD_IREGB){
			if(suffix_class & AC_SIMD_KREG){
			    extended_register_format[1] = 'k';
			    strcpy(op2, "%\16");}
			else {
			    extended_register_format[1] = 'i';
			    strcpy(op2, "%{");}
			}
		    if(suffix_class & AC_SIMD_ENCODE_S12)
			strcpy(op2,"%\14");
		    nop = 2;
		    }
		else
		    {
		    if (syntax_class & AC_SIMD_SYNTAX_VC0){
			extended_register_format[1] = 'c';
			strcpy (op2, "%B,");
			}
		    if(syntax_class & AC_SIMD_SYNTAX_VU0){
			extended_register_format[1] = '0';
			strcpy(op2, "%u,");
			}
		    if(syntax_class & AC_SIMD_SYNTAX_VL0){
			extended_register_format[1] = '0';
			strcpy(op2, "%L,");
			suffix_class |= AC_SIMD_ZERVB;
			}
		    if(syntax_class_modifiers & AC_SIMD_IREGB){
			if(suffix_class & AC_SIMD_KREG){
			    extended_register_format[1] = 'k';
			    strcpy(op2, "%\16,");}
			else {
			    extended_register_format[1] = 'i';
			    strcpy(op2, "%{,");}
			}
		    nop = 3;
		    }

		if(!strcmp(op2,""))
		    printf ("SYNTAX_v00 .. unknown op2:%s\n",instruction_name);
		}
	  break;
	case AC_SIMD_SYNTAX_C00:
	    extended_register_format[0] = 'c';
	    if(suffix_class & AC_SIMD_EXTEND2){
		strcpy(op1," %B");
		if(syntax_class_modifiers & AC_SIMD_IREGB){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[0] = 'k';
			strcpy(op1," %\16");}
		    else {
			extended_register_format[0] = 'i';
			strcpy(op1," %{");}
		    }
		if(syntax_class & AC_SIMD_SYNTAX_VVI){
		    if(suffix_class & AC_SIMD_KREG){
			extended_register_format[1] = 'k';
			strcpy(op2,",%\17");}
		    else {
			extended_register_format[1] = 'i';
			strcpy(op2,",%}");}
		    }
		extended_register_format[1] = '0';
		if(syntax_class& AC_SIMD_SYNTAX_VVC){
		    extended_register_format[1] = 'c';
		    strcpy(op2,",%C");
		    }
		if (suffix_class & AC_SIMD_ENCODE_U8)
		    strcpy (op2, ",%?");
		if(suffix_class & AC_SIMD_ENCODE_U6)
		    strcpy(op2,",%u");
		if(suffix_class & AC_SIMD_ENCODE_U16)
		    strcpy(op2,",%\20");
		if(syntax_class & AC_SIMD_SYNTAX_VL0)
		    strcpy(op2,",%L");
		nop = 2;
		break;
		}
	    strcpy(op1," %A,");
	    extended_register_format[0] = 'c';
	    if(syntax_class_modifiers & AC_SIMD_IREGA){
		if(suffix_class & AC_SIMD_KREG){
		    extended_register_format[0] = 'k';
		    strcpy(op1, " %\15,");}
		else {
		    extended_register_format[0] = 'i';
		    strcpy(op1, " %\13,"); }
		}
	    strcpy(op2,"%B,");
	    extended_register_format[1] = 'c';
	    if(syntax_class_modifiers & AC_SIMD_IREGB){
		if(suffix_class & AC_SIMD_KREG){
		    extended_register_format[1] = 'k';
		    strcpy(op2,"%\16,");}
		else {
		    extended_register_format[2] = 'i';
		    strcpy(op2,"%{,");}
		}
	    extended_register_format[2] = 'c';
	    if (suffix_class & AC_SIMD_ENCODE_U8)
		strcpy (op3, "%?");
	    if(suffix_class & AC_SIMD_ENCODE_U6)
		strcpy(op3,"%u");
	    if(suffix_class & AC_SIMD_ENCODE_U16)
		strcpy(op3,"%\20");
	    if(suffix_class & AC_SIMD_ENCODE_S12)
		strcpy(op3,"%\14");
	    nop = 3;
	    break;
	case AC_SIMD_SYNTAX_0:
	    extended_register_format[0] = '0';
	    if (syntax_class & AC_SIMD_SYNTAX_C){
		extended_register_format[0] = 'c';
		strcpy (op1, " %C");
		}
	    else if (suffix_class & AC_SIMD_ENCODE_U6)
		strcpy (op1, " %u");
	    else if (suffix_class & AC_SIMD_ENCODE_U16)
		strcpy (op1, " %\20");
	    else if (syntax_class & AC_SIMD_SYNTAX_VVI){
		if(suffix_class & AC_SIMD_KREG){
		    extended_register_format[0] = 'k';
		    strcpy (op1, " %\17");}
		else {
		    extended_register_format[0] = 'i';
		    strcpy (op1, " %}");}
		}
	    if(strcmp(op1,""))
		nop = 1;
	    else
		nop = 0;
	  break;


	case AC_SIMD_SYNTAX_C0:
/* special case of instruction with two constant 8 bit operands fitting
 * into sixteen bit constant field
 */
	    extended_register_format[0] = '0';
	    extended_register_format[1] = '0';

	    if((suffix_class & AC_SIMD_ENCODE_U6) &&
	       (suffix_class & AC_SIMD_ENCODE_U16)){
		strcpy(op1," %\21");
		strcpy(op2,",%\22");
		nop = 2;
		break;
		}
	    strcpy (op1, " %B,");
	    extended_register_format[0] = 'c';
	    if(syntax_class_modifiers & AC_SIMD_SYNTAX_DISC){
		extended_register_format[0] = '0';
		strcpy(op1, " %u,");
		}
	    if(syntax_class_modifiers & AC_SIMD_IREGB){
		if(suffix_class & AC_SIMD_KREG){
		    extended_register_format[0] = 'k';
		    strcpy(op1, " %\16,");}
		else {
		    extended_register_format[0] = 'i';
		    strcpy(op1, " %{,"); }
		}
	    if (syntax_class & AC_SIMD_SYNTAX_CC){
		extended_register_format[1] = 'c';
		strcpy (op2, "%C");
		}
	    if(syntax_class & AC_SIMD_SYNTAX_VVI){
		if(suffix_class & AC_SIMD_KREG){
		    extended_register_format[1] = 'k';
		    strcpy(op2, "%\17");}
		else {
		    extended_register_format[1] = 'i';
		    strcpy(op2, "%}");}
		}
	    if(suffix_class & AC_SIMD_ENCODE_U6){
		extended_register_format[1] = '0';
		strcpy(op2,"%u");
		}
	    if(suffix_class & AC_SIMD_ENCODE_U16){
		extended_register_format[1] = '0';
		strcpy(op2,"%\20");
		}
	    if(suffix_class & AC_SIMD_ENCODE_U8){
		extended_register_format[1] = '0';
		strcpy(op2,"%?");
		}
	    if(suffix_class & AC_SIMD_ENCODE_LIMM)
		{
		suffix_class |= AC_SIMD_ZERVC;
		strcpy(op2,"%L");
		extended_register_format[1] = '0';
		}
	    nop = 2;
	    if (!strcmp(op2,""))
		printf("SYNTAX_C0 op2 not found:%s\n",instruction_name);

	  break;


	case AC_SIMD_SYNTAX_D0:
	    extended_register_format[0] = 'd';
	  strcpy (op1, " %<,");
	  if (syntax_class & AC_SIMD_SYNTAX_CC){
		extended_register_format[0] = 'c';
		strcpy (op1, " %B,");
	      }
	  if (syntax_class & AC_SIMD_SYNTAX_DC){
	      extended_register_format[1] = 'c';
	      strcpy (op2, "%C");
	      }
	  if (syntax_class & AC_SIMD_SYNTAX_VVI){
	      if(suffix_class & AC_SIMD_KREG){
		  extended_register_format[1] = 'k';
		  strcpy (op2, "%\17");}
	      else {
		  extended_register_format[1] = 'i';
		  strcpy (op2, "%}");}
	      }
	  if(syntax_class & AC_SIMD_SYNTAX_VL0){
	      extended_register_format[1] = '0';
	      strcpy(op2, "%L");
	      if( suffix_class & (AC_SIMD_EXTEND1 | AC_SIMD_EXTEND2))
		  suffix_class |= AC_SIMD_ZERVC;
	      else
		  suffix_class |= AC_SIMD_ZERVB;
	      }

	  nop = 2;
	  if (!strcmp(op2,""))//temp .. please remove
	    printf("SYNTAX_D0 op2 not found:%s\n",instruction_name);
	  break;


	case AC_SIMD_SYNTAX_VD:
	  extended_register_format[0] = 'v';
	  extended_register_format[1] = 'd';
	  strcpy (op1, " %(,");
	  strcpy (op2, "%>");
	  nop = 2;
	  break;


	default:
	    printf("unmapped syntax class found:%s\n",instruction_name);
	  break;

	}

      insn = mask =0;
      insn2 = mask2 = 0;

      insn = (major_opcode << 27); /*SIMD Major Opcode*/
      mask = (-1 & 0xfc000000);

      if(suffix_class&AC_SUFFIX_LANEMASK){
	  syntax_class_modifiers |= ARC_SIMD_LANEMASK;
	  }
      if(suffix_class & AC_SIMD_EXTENDED){
	  insn |= ((sub_opcode & 0x3f) << 16);
	  mask |= 0x3f << 16;
	  }
      if(suffix_class & AC_SIMD_EXTEND2 || suffix_class&AC_SIMD_EXTEND1||
	  suffix_class & AC_SIMD_EXTEND3){
	  insn |= (((sub_opcode >> 8) & 0x3f));
	  mask |= 0x3f;
	  if((insn&0x3f)==0x3f){
	      insn |= (((sub_opcode >> 16) & 0x7)) << 24;
	      mask |= 0x7 << 24;
	      }
	  }
      if(suffix_class & AC_SIMD_EXTEND3){
	  insn |= (((sub_opcode >> 16) & 0x7) << 24);
	  mask |= 0x7 << 24;
	  }
      if(suffix_class & (AC_SIMD_EXTENDED|AC_SIMD_EXTEND2|AC_SIMD_EXTEND3)){
	  insn2 |= (((sub_opcode>>24) & 3) << 30);
	  mask2 |= 3 << 30;
	  }
      if (suffix_class & AC_SIMD_FLAG_SET)
	insn |= (1 << 15);
      mask |= (1 << 15);

      syntax_class_modifiers &= ~(ARC_SIMD_SCALE1|ARC_SIMD_SCALE2|ARC_SIMD_SCALE3|ARC_SIMD_SCALE4);
      syntax_class   &= ~(ARC_SIMD_SCALE1|ARC_SIMD_SCALE2|ARC_SIMD_SCALE3|ARC_SIMD_SCALE4);

      if (suffix_class
	  & (AC_SIMD_SCALE_4 | AC_SIMD_SCALE_3 | AC_SIMD_SCALE_2
	     | AC_SIMD_SCALE_1 |AC_SIMD_SCALE_0))
	{
	  sprintf (operand_string,"%s[%s%s]",op1,op2,op3);
	  insn |= (1 << 23);

	  switch(suffix_class
	  & (AC_SIMD_SCALE_4 | AC_SIMD_SCALE_3 | AC_SIMD_SCALE_2
	     | AC_SIMD_SCALE_1|AC_SIMD_SCALE_0))
	    {
	    case AC_SIMD_SCALE_1:
	      extended_instruction_flags[2] |= FLAG_SCALE_1 >> 8;
	      syntax_class_modifiers |= ARC_SIMD_SCALE1;
	      break;
	    case AC_SIMD_SCALE_2:
	      extended_instruction_flags[2] |= FLAG_SCALE_2 >> 8;
	      syntax_class_modifiers |= ARC_SIMD_SCALE2;
	      break;
	    case AC_SIMD_SCALE_3:
	      extended_instruction_flags[2] |= FLAG_SCALE_3 >> 8;
	      syntax_class_modifiers |= ARC_SIMD_SCALE3;
	      break;
	    case AC_SIMD_SCALE_4:
	      extended_instruction_flags[2] |= FLAG_SCALE_4 >> 8;
	      syntax_class_modifiers |= ARC_SIMD_SCALE4;
	      break;
	    case AC_SIMD_SCALE_0:
		break;
	    default:
	      abort();
	      break;
	    }
	}
      else {
	sprintf (operand_string,"%s%s%s",op1,op2,op3);
	  }

      if (suffix_class & AC_SIMD_ENCODE_S12)
	  extended_instruction_flags[2] |= FLAG_EXT_S16 >> 8;
      if (suffix_class & AC_SIMD_ENCODE_U16)
	  extended_instruction_flags[2] |= FLAG_EXT_S16 >> 8;

      if (suffix_class & AC_SIMD_FLAG1_SET)
	{
	  insn |= (1 << 23);
	  mask |= (1 << 23);
	}

      if (suffix_class & AC_SIMD_FLAG2_SET)
	{
	  insn |= (1 << 22);
	  mask |= (1 << 22);
	}
      /*FIXME:Bit 22 and 23  to be taken care of*/
      /* OP3
	27-31 - major opcode (6 in this case)
	26,25,24- op2-lower 3 bits
	23- unknown
	22- set when instrn name ends in i
	21-17 - subopcode if u8 present
	21-16 - subopcode if non-u8 present
	15- set if flag_set
	14-12- op2- high 3 bits
	11-6 - op3 bits
	5-0- op 1 bits
      */
      /*NOP=2
	27-31-Major OPcode
	26,25,24 - opb lower 3 bits
	23-unknown
	22 -set when instrn name ends in i
	21-16=101111
	15- set if flag_set
	14-12-opb high 3 bits
	11-6- opc bits
	5-0- sub_opcode
      */



      switch (nop)
	{
	case 3:
	    extended_instruction_flags[3] |= FLAG_3OP;
	  if (suffix_class & AC_SIMD_ENCODE_U8)
	    {
	      insn |= ((sub_opcode & 0x1f) << 17);
	      mask |= (0x1f << 17);
	    }
	  else
	    {
	      insn |= ((sub_opcode & 0x3f) << 16);
	      mask |= (0x3f << 16);
	    }
	  break;

	case 2:
	    extended_instruction_flags[3] |= FLAG_2OP;
	    if(suffix_class&(AC_SIMD_EXTENDED|AC_SIMD_EXTEND2|AC_SIMD_EXTEND3|AC_SIMD_EXTEND1)){
		if (suffix_class & AC_SIMD_ENCODE_U8)
		    {
		    insn |= ((sub_opcode & 0x1f) << 17);
		    mask |= (0x1f << 17);
		    }
		else
		    {
		    insn |= ((sub_opcode & 0x3f) << 16);
		    mask |= (0x3f << 16);
		    }
		break;
	      }
	  else
	      {
	      if(suffix_class & AC_SIMD_ENCODE_U8){
		  insn |= ((sub_opcode & 0x1f) << 17);
		  mask |= (0x1f << 17);
		  }
	      else
		  {
		  insn |= ((sub_opcode & 0x3f) << 16);
		  mask |= (0x3f << 16);
		  }
	      }
	  break;

	case 1:
	    extended_instruction_flags[3] |= FLAG_1OP;
	    if(suffix_class&(AC_SIMD_EXTENDED|AC_SIMD_EXTEND2|AC_SIMD_EXTEND3|AC_SIMD_EXTEND1)){
		if (suffix_class & AC_SIMD_ENCODE_U8)
		    {
		    insn |= ((sub_opcode & 0x1f) << 17);
		    mask |= (0x1f << 17);
		    }
		else
		    {
		    insn |= ((sub_opcode & 0x3f) << 16);
		    mask |= (0x3f << 16);
		    if((insn & 0x3f) == 0x3f){
			insn |= (((sub_opcode >> 16) & 0x7) << 24);
			mask |= 7 << 24;
			}
		    }
		break;
		}
	    else
		{
		insn |= (0x2f << 16);
		mask |= (0x3f << 16);

		insn |= (0x3f << 0);
		mask |= (0x3f << 0);

		insn |= ((sub_opcode & 0x7) << 24);
		mask |= ((0x7) << 24);
		}
	  break;
	case 0:
	    extended_instruction_flags[3] |= FLAG_NOP;
	    if(suffix_class&(AC_SIMD_EXTENDED|AC_SIMD_EXTEND2|AC_SIMD_EXTEND3|AC_SIMD_EXTEND1)){
		if (suffix_class & AC_SIMD_ENCODE_U8)
		    {
		    insn |= ((sub_opcode & 0x1f) << 17);
		    mask |= (0x1f << 17);
		    }
		else
		    {
		    insn |= ((sub_opcode & 0x3f) << 16);
		    mask |= (0x3f << 16);
		    if((insn & 0x3f) == 0x3f){
			insn |= (((sub_opcode >> 16) & 0x7) << 24);
			mask |= 7 << 24;
			}
		    }
		break;
		}
	    else
		{
		insn |= (0x2f << 16);
		mask |= (0x3f << 16);
		insn |= ((sub_opcode & 0x3f) << 16);
		mask |= ((0x3f) << 16);
		}
	    break;

	default:
	  as_fatal ("Invalid syntax\n");
	  break;
	}
      if(syntax_class & AC_SIMD_SYNTAX_VbI0 ||
	 syntax_class & AC_SIMD_SYNTAX_Vb00){
	  extended_operand_format2[0] |= '[';
	  extended_operand_format2[1] |= '%';
	  extended_operand_format2[2] |= 'o';
	  extended_operand_format3[0] |= '%';
	  extended_operand_format3[1] |= 'o';
	  extended_operand_format3[2] |= ']';
	  }

      xmitsuffix = suffix_class & (AC_SUFFIX_FLAG | AC_SUFFIX_COND | ARC_SIMD_LANEMASK | AC_SIMD_ZERVA | AC_SIMD_ZERVB | AC_SIMD_ZERVC | AC_SIMD_SETLM);

      syntax_class &= ~(ARC_SIMD_SCALE1 | ARC_SIMD_SCALE2 |
			ARC_SIMD_SCALE3 | ARC_SIMD_SCALE4);
      if(suffix_class&(AC_SIMD_EXTENDED | AC_SIMD_EXTEND2 | AC_SIMD_EXTEND3)){
	  arc_add_long_ext_inst(instruction_name,operand_string,
			   insn,mask,insn2,mask2,
				(syntax_class|syntax_class_modifiers),
				xmitsuffix);
	  }
      else
      arc_add_ext_inst (instruction_name, operand_string,
			insn,
			mask,
			(syntax_class | syntax_class_modifiers),
			xmitsuffix);

      break;

    default:
      as_bad("Invalid syntax\n");

    }
  return nop;
}


/* This function generates the operand strings based on the syntax class and
   syntax class modifiers and does some error checking.  */
static void
arc_generate_extinst16_operand_strings (char *instruction_name,
					unsigned char major_opcode,
					unsigned char sub_opcode,
					unsigned long syntax_class,
					unsigned long syntax_class_modifiers ATTRIBUTE_UNUSED,
					unsigned long suffix_class ATTRIBUTE_UNUSED)
{
  if((major_opcode > 0x0B) || (major_opcode < 0x08))
    {
      as_bad ("major opcode not in range [0x08-0x0B]");
      ignore_rest_of_line ();
      return;
    }

  switch(syntax_class &
	 (AC_SYNTAX_3OP | AC_SYNTAX_2OP | AC_SYNTAX_1OP | AC_SYNTAX_NOP))
    {
    case AC_SYNTAX_3OP:
    case AC_SYNTAX_2OP:

      if (sub_opcode < 0x01 || sub_opcode > 0x1f)
	as_bad ("Subopcode not in range [0x01 - 0x1f]\n");

      arc_add_ext_inst (instruction_name, " %b,%b,%c",
			INSN_16(major_opcode, 0, 0, sub_opcode),
			INSN_16(-1, 0, 0, -1),
			syntax_class, 0);
      break;

    case AC_SYNTAX_1OP:

      /* if (sub_opcode < 0x00 || sub_opcode > 0x06) */
      if (sub_opcode > 0x06)
	as_bad ("Subopcode not in range [0x00 - 0x06]\n");

      arc_add_ext_inst (instruction_name, " %b",
			INSN_16(major_opcode, 0, sub_opcode, 0),
			INSN_16(-1, 0, -1, -1),
			syntax_class, 0);
      break;

    case AC_SYNTAX_NOP:

      /*  if (sub_opcode < 0x00 || sub_opcode > 0x07) */
      if (sub_opcode > 0x07)
	as_bad ("Subopcode not in range [0x00 - 0x07]\n");

      arc_add_ext_inst (instruction_name, "",
			INSN_16(major_opcode, sub_opcode, 0x7, 0),
			INSN_16(-1, 0, -1, -1),
			syntax_class, 0);
      break;

    default:

      as_bad("Invalid syntax or unimplemented class\n");
    }
}

/* This function does the parsing of the .extinstruction directive and puts
   the instruction definition into the extension map while assembling for
   the ARCompact.  This function should be used for the ARCTangent too.  */
static void
arc_ac_extinst (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  unsigned int i;
  char c, *p;
  unsigned char major_opcode;
  unsigned long sub_opcode,sub_op;
  unsigned long syntax_class = 0;
  unsigned long syntax_class_modifiers = 0;
  unsigned long suffix_class = 0;
  char *instruction_name;
  unsigned int name_length;
  int nops;

  segT old_sec;
  int old_subsec;


  /* Get all the parameters.  */

  /* Start off with the name of the instruction.  */

  SKIP_WHITESPACE ();

  instruction_name = input_line_pointer;
  c = get_symbol_end ();
  instruction_name = xstrdup (instruction_name);
  name_length = strlen(instruction_name);
  *input_line_pointer = c;

  /* Get major opcode.  */

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after instruction name");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','.  */
  major_opcode = get_absolute_expression ();
  if ((major_opcode == 9 || major_opcode ==10)  && (extinsnlib & (SIMD_INSN)))
    syntax_class |= (AC_SYNTAX_SIMD);

  /* Get sub-opcode.  */

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after majoropcode");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','.  */
  sub_opcode = get_absolute_expression ();
  /* Get suffix class.  */

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after sub opcode");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','.  */

  while (1)
    {
      SKIP_WHITESPACE ();
      for (i=0 ; i<AC_MAXSUFFIXCLASS ; i++)
	if (!strncmp (ac_suffixclass[i].name,
		     input_line_pointer,
		     ac_suffixclass[i].len))
	  {
	    suffix_class |= ac_suffixclass[i].class;
	    input_line_pointer += ac_suffixclass[i].len;
	    break;
	  }

      if(i == AC_MAXSUFFIXCLASS)
	{
	  as_bad ("invalid suffix class");
	  ignore_rest_of_line ();
	  return;
	}

      SKIP_WHITESPACE ();

      if (*input_line_pointer == '|')
	input_line_pointer++;		/* skip '|'.  */
      else
	if (*input_line_pointer == ',')
	  break;
	else
	  {
	    as_bad ("invalid character '%c' in expression",
		    *input_line_pointer);
	    ignore_rest_of_line ();
	    return;
	  }
    }

  /* Get syntax class and syntax class modifiers.  */

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after suffix class");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','.  */

  while (1)
    {
      SKIP_WHITESPACE ();
      for (i=0 ; i<AC_MAXSYNTAXCLASSMODIFIER ; i++)
	if (!strncmp (ac_syntaxclassmodifier[i].name,
		     input_line_pointer,
		     ac_syntaxclassmodifier[i].len))
	  {
	    syntax_class_modifiers |= ac_syntaxclassmodifier[i].class;
	    input_line_pointer += ac_syntaxclassmodifier[i].len;
	    break;
	  }

      if(i == AC_MAXSYNTAXCLASSMODIFIER)
	{
	  for(i= 0 ; i<AC_MAXSYNTAXCLASS ; i++)
	    if(!strncmp(ac_syntaxclass[i].name,
			input_line_pointer,
			ac_syntaxclass[i].len))
	      {
		syntax_class |= ac_syntaxclass[i].class;
		input_line_pointer += ac_syntaxclass[i].len;
		break;
	      }

	  if(i == AC_MAXSYNTAXCLASS)
	    {
	      as_bad ("invalid syntax");
	      ignore_rest_of_line ();
	      return;
	    }
	}

      SKIP_WHITESPACE ();
      if(*input_line_pointer == '|')
	input_line_pointer++;		/* skip '|'.  */
      else
	if (is_end_of_line[(unsigned char) *input_line_pointer])
	  break;
	else
	  {
	    as_bad ("invalid character '%c' in expression",
		    *input_line_pointer);
	    ignore_rest_of_line ();
	    return;
	  }
    }

  /* Done getting all the parameters.  */

  /* Make extension instruction syntax strings.  */
  /* catch a few oddball cases */
  if(syntax_class&(AC_SIMD_SYNTAX_0|AC_SIMD_SYNTAX_C0|AC_SIMD_SYNTAX_VVV|
		   AC_SIMD_SYNTAX_VV|AC_SIMD_SYNTAX_VV0|AC_SIMD_SYNTAX_D0)){
      syntax_class |= AC_SYNTAX_SIMD;

      }
  nops = 0;
/* don't use extended format unless needed*/
  use_extended_instruction_format=0;
  if (!strncmp (instruction_name + name_length - 2, "_s", 2))
    arc_generate_extinst16_operand_strings (instruction_name,
				    major_opcode, sub_opcode,
				    syntax_class, syntax_class_modifiers,
				    suffix_class);

  else
    nops = arc_generate_extinst32_operand_strings (instruction_name,
				    major_opcode, sub_opcode,
				    syntax_class, syntax_class_modifiers,
				    suffix_class);
  sub_op = sub_opcode;
  if(syntax_class & AC_SYNTAX_SIMD){
      if(major_opcode==0xa){
	  if(suffix_class & AC_SIMD_FLAG2_SET)
	      sub_opcode |= 0x40000000;
	  if(suffix_class & AC_SIMD_FLAG1_SET)
	      sub_opcode |= 0x80000000;
	  }
      sub_opcode = sub_opcode | (nops << 28);
      if(suffix_class & AC_SIMD_ENCODE_U8)
	  sub_opcode |= 0x4000000;
      } /* end if(syntax_class & AC_SYNTAX_SIMD) */

  /* Done making the extension syntax strings.  */

  /* OK, now that we know what this inst is, put a description in the
     arc extension section of the output file.  */

  old_sec    = now_seg;
  old_subsec = now_subseg;

  arc_set_ext_seg (EXT_INSTRUCTION32, syntax_class, major_opcode, sub_opcode);
  switch( use_extended_instruction_format){
  case 1:
      sub_op = sub_opcode & 0x3f;
      if(syntax_class & AC_SYNTAX_SIMD){
	  if(suffix_class & (AC_SIMD_EXTEND2 | AC_SIMD_EXTEND1)){
	      sub_op = (sub_opcode >> 8) & 0x3f;
	      }
	  if(suffix_class & AC_SIMD_EXTEND3){
	      sub_op = (sub_opcode >>16) & 0x3f;
	      }
	  }
      p = frag_more(OPD_FORMAT_SIZE*3+RCLASS_SET_SIZE+13);
      *p = OPD_FORMAT_SIZE*3+RCLASS_SET_SIZE+13+name_length+1;
      p++;
      *p = EXT_INSTRUCTION32_EXTENDED;
      p++;
      *p = major_opcode;
      p++;
      *p = sub_op;
      p++;
      for(i = 0; i < RCLASS_SET_SIZE; i++){
	  *p = extended_register_format[i];
	  p++;
	  }
      for(i = 0; i < OPD_FORMAT_SIZE; i++){
	  *p = extended_operand_format1[i];
	  p++;
	  }
      for(i = 0; i < OPD_FORMAT_SIZE; i++){
	  *p = extended_operand_format2[i];
	  p++;
	  }
      for(i = 0; i < OPD_FORMAT_SIZE; i++){
	  *p = extended_operand_format3[i];
	  p++;
	  }
      for(i = 0; i < 4; i++){
	  *p = extended_instruction_flags[i];
	  p++;
	  }
      if(suffix_class & (AC_SIMD_EXTENDED|AC_SIMD_EXTEND2|AC_SIMD_EXTEND3))
	  *p = 1;
      else
	  *p = 0;
      p++;
      for(i = 0; i < 3; i++){
	  *p = 0;
	  p++;
	  }
      *p = extended_instruction_flags[3];
      p++;
      break;
    case 0:
      p = frag_more (1);
      *p = 5 + name_length + 1;
      p = frag_more (1);

      /* By comparing with the Metaware assembler, EXT_INSTRUCTION should be
	 0x40 as opposed to 0x00 which we have defined. arc-ext.h.  */

      *p = EXT_INSTRUCTION32;
      p = frag_more (1);
      *p = major_opcode;
      p = frag_more (1);
      *p = sub_opcode;
      p = frag_more (1);
      *p = syntax_class
	  | (syntax_class_modifiers & (AC_OP1_DEST_IGNORED
				       | AC_OP1_MUST_BE_IMM)? 0x10:0);
      break;
  case 2:
      demand_empty_rest_of_line();
      free (instruction_name);
      return;
      }

  p = frag_more (name_length + 1);
  strcpy (p, instruction_name);

  subseg_set (old_sec, old_subsec);
  demand_empty_rest_of_line ();

  free (instruction_name);
}

/* Here ends all the ARCompact extension instruction assembling stuff.  */

static void
arc_common (int localScope)
{
  char *name;
  char c;
  char *p;
  int align, size;
  symbolS *symbolP;

  name = input_line_pointer;
  c = get_symbol_end ();
  /* just after name is now '\0'  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after symbol name");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */
  size = get_absolute_expression ();

  if (size < 0)
    {
      as_bad ("negative symbol length");
      ignore_rest_of_line ();
      return;
    }

  *p = 0;
  symbolP = symbol_find_or_make (name);
  *p = c;

  if (S_IS_DEFINED (symbolP) && ! S_IS_COMMON (symbolP))
    {
      as_bad ("ignoring attempt to re-define symbol");
      ignore_rest_of_line ();
      return;
    }
  if (((int) S_GET_VALUE (symbolP) != 0) \
      && ((int) S_GET_VALUE (symbolP) != size))
    {
      as_warn ("length of symbol \"%s\" already %ld, ignoring %d",
	       S_GET_NAME (symbolP), (long) S_GET_VALUE (symbolP), size);
    }
  gas_assert (symbolP->sy_frag == &zero_address_frag);

  /* Now parse the alignment field.  This field is optional for
     local and global symbols. Default alignment is zero.  */
  if (*input_line_pointer == ',')
    {
      input_line_pointer++;
      align = get_absolute_expression ();
      if (align < 0)
	{
	  align = 0;
	  as_warn ("assuming symbol alignment of zero");
	}
    }
  else if (localScope == 0)
    align = 0;

  else
    {
      as_bad ("Expected comma after length for lcomm directive");
      ignore_rest_of_line ();
      return;
    }


  if (localScope != 0)
    {
      segT old_sec;
      int old_subsec;
      char *pfrag;

      old_sec    = now_seg;
      old_subsec = now_subseg;
      record_alignment (bss_section, align);
      subseg_set (bss_section, 0);  /* ??? subseg_set (bss_section, 1); ???  */

      if (align)
	/* Do alignment.  */
	frag_align (align, 0, 0);

      /* Detach from old frag.  */
      if (S_GET_SEGMENT (symbolP) == bss_section)
	symbolP->sy_frag->fr_symbol = NULL;

      symbolP->sy_frag = frag_now;
      pfrag = frag_var (rs_org, 1, 1, (relax_substateT) 0, symbolP,
			(offsetT) size, (char *) 0);
      *pfrag = 0;

      S_SET_SIZE       (symbolP, size);
      S_SET_SEGMENT    (symbolP, bss_section);
      S_CLEAR_EXTERNAL (symbolP);
      symbol_get_obj (symbolP)->local = 1;
      subseg_set (old_sec, old_subsec);
    }
  else
    {
      S_SET_VALUE    (symbolP, (valueT) size);
      S_SET_ALIGN    (symbolP, align);
      S_SET_EXTERNAL (symbolP);
      S_SET_SEGMENT  (symbolP, bfd_com_section_ptr);
    }

  symbolP->bsym->flags |= BSF_OBJECT;

  demand_empty_rest_of_line ();
}

/* Select the cpu we're assembling for.  */

static void
arc_option (int ignore ATTRIBUTE_UNUSED)
{
  int mach;
  char c;
  char *cpu;

  cpu = input_line_pointer;
  c = get_symbol_end ();
  mach = arc_get_mach (cpu);
  *input_line_pointer = c;

  /* If an instruction has already been seen, it's too late.  */
  if (cpu_tables_init_p)
    {
      as_bad ("\".option\" directive must appear before any instructions");
      ignore_rest_of_line ();
      return;
    }

  if (mach == -1)
    goto bad_cpu;


  if (!mach_type_specified_p)
    {
	arc_mach_type = mach;
	if (mach == bfd_mach_arc_a4)
	  as_bad (_("Support for Arctangent-A4 has been removed"));
	arc_opcode_init_tables (arc_get_opcode_mach (arc_flags ? arc_flags : mach,
						     target_big_endian));

      if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, mach))
	as_fatal ("could not set architecture and machine");

      mach_type_specified_p = 1;
    }
  else
    if (arc_mach_type != mach)
      as_warn ("Command-line value overrides \".option\" directive");

  demand_empty_rest_of_line ();

  return;

 bad_cpu:
  as_bad ("invalid identifier for \".option\"");
  ignore_rest_of_line ();
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

/* Equal to MAX_PRECISION in atof-ieee.c  */
#define MAX_LITTLENUMS 6

char *
md_atof (int type, char *litP, int *sizeP)
{
  int prec;
  LITTLENUM_TYPE words[MAX_LITTLENUMS];
  LITTLENUM_TYPE *wordP;
  char *t;

  switch (type)
    {
    case 'f':
    case 'F':
      prec = 2;
      break;

    case 'd':
    case 'D':
      prec = 4;
      break;

    default:
      *sizeP = 0;
      return "bad call to md_atof";
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;
  *sizeP = prec * sizeof (LITTLENUM_TYPE);
  for (wordP = words; prec--;)
    {
      md_number_to_chars (litP, (valueT) (*wordP++), sizeof (LITTLENUM_TYPE));
      litP += sizeof (LITTLENUM_TYPE);
    }

  return NULL;
}

/* Convert from target byte order to host byte order
   The size (N) -4 is used to indicate a 4-byte limm value, which is
   encoded as 'middle-endian' for a little-endian target.  */

static valueT
md_chars_to_number (buf, n)
     char *buf;
     int n;
{
  valueT result;
  unsigned char *where = (unsigned char *) buf;

  result = 0;

  if (target_big_endian)
    {
      if (n == -4) n = 4;
      switch (n)
	{
	case 2:
	case 4:
	  while (n--)
	    {
	      result <<= 8;
	      result |= (*where++ & 255);
	    }
	  break;
	default:
	  abort ();
	}
    }
  else
    {
      switch (n)
	{
	case 2:
	case 4:
	  while (n--)
	    {
	      result <<= 8;
	      result |= (where[n] & 255);
	    }
	  break;
	case -4:
	  result |= ((where[0] & 255) << 16);
	  result |= ((where[1] & 255) << 24);
	  result |= (where[2] & 255);
	  result |= ((where[3] & 255) << 8);
	  break;
	default:
	  abort ();
	}
    }

  return result;
}

/* Write a value out to the object file, using the appropriate
   endianness.  The size (N) -4 is used internally in tc-arc.c to
   indicate a 4-byte limm value, which is encoded as 'middle-endian'
   for a little-endian target.  */

void
md_number_to_chars (char *buf, valueT val, int n)
{
  void (*endian) (char *, valueT, int) = (target_big_endian)
    ? number_to_chars_bigendian : number_to_chars_littleendian;

  switch (n)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 8:
      endian (buf, val, n);
      break;
    case -4:
      endian (buf, (val & 0xffff0000) >> 16, 2);
      endian (buf + 2, val & 0xffff, 2);
      break;
    default:
      abort ();
    }
}

/* Round up a section size to the appropriate boundary.  */

valueT
md_section_align (segT segment, valueT size)
{
  int align = bfd_get_section_alignment (stdoutput, segment);

  return ((size + (1 << align) - 1) & (-1 << align));
}

/* We don't have any form of relaxing.  */

int
md_estimate_size_before_relax (fragS *fragp ATTRIBUTE_UNUSED,
			       asection *seg ATTRIBUTE_UNUSED)
{
  as_fatal (_("md_estimate_size_before_relax\n"));
  return 1;
}

/* Convert a machine dependent frag.  We never generate these.  */

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED,
		 asection *sec ATTRIBUTE_UNUSED,
		 fragS *fragp ATTRIBUTE_UNUSED)
{
  as_fatal (_("md_convert_frag\n"));
}

/* Parse an operand that is machine-specific.

   The ARC has a special %-op to adjust addresses so they're usable in
   branches.  The "st" is short for the STatus register.
   ??? Later expand this to take a flags value too.

   ??? We can't create new expression types so we map the %-op's onto the
   existing syntax.  This means that the user could use the chosen syntax
   to achieve the same effect.  */

void
md_operand (expressionS *expressionP)
{
  char *p = input_line_pointer;

  switch(*p)
    {
    case '%':
	{
	  /* It could be a register.  */
	  int i, l;
	  struct arc_ext_operand_value *ext_oper = arc_ext_operands;
	  p++;

	  while (ext_oper)
	    {
	      l = strlen (ext_oper->operand.name);
	      if (!strncmp (p, ext_oper->operand.name, l) && !ISALNUM (*(p + l)))
		{
		  input_line_pointer += l + 1;
		  expressionP->X_op = O_register;
		  expressionP->X_add_number = (offsetT) &ext_oper->operand;
		  return;
		}
	      ext_oper = ext_oper->next;
	    }
	  for (i = 0; i < arc_reg_names_count; i++)
	    {
	      l = strlen (arc_reg_names[i].name);
	      if (!strncmp (p, arc_reg_names[i].name, l) && !ISALNUM (*(p + l)))
		{
		  input_line_pointer += l + 1;
		  expressionP->X_op = O_register;
		  expressionP->X_add_number = (offsetT) &arc_reg_names[i];
		  break;
		}
	    }
	}
      break;
    case '@':
      /*
	If this identifier is prefixed with '@' then make the expression
	(operand) of the type O_symbol so that arc_parse_name will not
	treat it as a register.
      */
      input_line_pointer++;
      expressionP->X_op = O_symbol;
      expression (expressionP);
      break;
    }
}

/*
  This function is called from the function 'expression', it attempts
  to parse special names (in our case register names).  It fills in
  the expression with the identified register.  It returns 1 if it is
  a register and 0 otherwise.
*/

int
arc_parse_name (name, expressionP)
     const char *name;
     expressionS *expressionP;
{
  int i, l;
  struct arc_ext_operand_value *ext_oper = arc_ext_operands;

  /* By default, expressionP->X_op has O_illegal.  However whenever we
    encounter the '@' chatacter (which is handled in md_operand) we
    set the expression type to O_symbol.  Thereby we over-ride the
    register name being treated as a register if it is prefixed with
    '@'. */

  if(!assembling_instruction)
    return 0;

  if(expressionP->X_op == O_symbol)
    return 0;

  while (ext_oper)
    {
      l = strlen (ext_oper->operand.name);
      if (!strcasecmp (name, ext_oper->operand.name) && !ISALNUM (*(name + l)))
	{
	  expressionP->X_op = O_register;
	  expressionP->X_add_number = (offsetT) &ext_oper->operand;
	  return 1;
	}
      ext_oper = ext_oper->next;
    }
  for (i = 0; i < arc_reg_names_count; i++)
    {
      l = strlen (arc_reg_names[i].name);
      if (!strcasecmp (name, arc_reg_names[i].name) && !ISALNUM (*(name + l)))
	{
	  expressionP->X_op = O_register;
	  expressionP->X_add_number = (offsetT) &arc_reg_names[i];
	  return 1;
	  break;
	}
    }

#ifdef ENFORCE_AT_PREFIX
  as_bad ("Symbol %s not prefixed with '@'",name);
#endif
  return 0;
}

/* We have no need to default values of symbols.
   We could catch register names here, but that is handled by inserting
   them all in the symbol table to begin with.  */

symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
    /* The arc abi demands that a GOT[0] should be referencible as
       [ pc+_DYNAMIC@gotpc ].Hence we convert a _DYNAMIC@gotpc to
       a GOTPC reference to _GLOBAL_OFFSET_TABLE_  */
    if ((*name == '_' && *(name+1) == 'G'
	 && strcmp(name, GLOBAL_OFFSET_TABLE_NAME) == 0)
	||
	(*name == '_' && *(name+1) == 'D'
	 && strcmp(name, DYNAMIC_STRUCT_NAME) == 0))
    {
	if(!GOT_symbol)
	{
	    if(symbol_find(name))
		as_bad("GOT already in symbol table");

		GOT_symbol = symbol_new (GLOBAL_OFFSET_TABLE_NAME, undefined_section,
					 (valueT) 0, &zero_address_frag);
	};
	return GOT_symbol;
    }

    return 0;
}

/* Functions concerning expressions.  */

/* Parse a .byte, .word, etc. expression.

   ??? Not sure if TC_CONS_EXPRESSION is still wanted.  We originally
   defined this to process Arctangent-A4 specific constructs.
   The remaining difference to the default TC_CONS_EXPRESSION
   is that expr is called with expr_evaluate, not expr_normal.  */

void
arc_parse_cons_expression (expressionS *exp,
			   unsigned int nbytes ATTRIBUTE_UNUSED)
{
  expression_and_evaluate (exp);
}

/* Record a fixup for a cons expression.  */

void
arc_cons_fix_new (fragS *frag,
		  int where,
		  int nbytes,
		  expressionS *exp)
{
  if (nbytes == 4)
    {
      int reloc_type
	= get_arc_exp_reloc_type (BFD_RELOC_32, BFD_RELOC_32_PCREL, exp);
      fix_new_exp (frag, where, nbytes, exp, 0, reloc_type);
    }
  else
    {
      static int bfd_reloc_map[] = {BFD_RELOC_NONE,BFD_RELOC_8,BFD_RELOC_16,
				BFD_RELOC_24,BFD_RELOC_32,BFD_RELOC_NONE,
				BFD_RELOC_NONE,BFD_RELOC_64 };
      fix_new_exp (frag, where, nbytes, exp, 0, bfd_reloc_map[nbytes]);
    }
}

/* Functions concerning relocs.  */

/* The location from which a PC relative jump should be calculated,
   given a PC relative reloc.  */

long
md_pcrel_from_section (fixS *fixP, segT sec)
{
  if (fixP->fx_addsy != (symbolS *) NULL
      && (generic_force_reloc (fixP)
	  || (S_GET_SEGMENT (fixP->fx_addsy) != sec)))
    {
      /* The symbol is undefined.  Let the linker figure it out.  */
      return 0;
    }

  /* Return the value that will be seen at run time in pcl, which is the
     address of the current instruction masked with ~3.  */
  return (fixP->fx_frag->fr_address + fixP->fx_where) & ~0x3;
}

/* Get the relocation for the sda symbol reference in insn */
static int
arc_get_sda_reloc (arc_insn insn, int compact_insn_16)
{
  if (ac_add_reg_sdasym_insn (insn))
    return BFD_RELOC_ARC_SDA32_ME;

  /* Refer to opcodes/arc-opc.c for 'insn to return value' mappings for this
     function.  */
  switch (ac_get_load_sdasym_insn_type (insn, compact_insn_16))
    {
    case 0:
      return BFD_RELOC_ARC_SDA_LDST2;
    case 1:
      return BFD_RELOC_ARC_SDA_LDST;
    case 2:
      return BFD_RELOC_ARC_SDA_LDST1;

    case 10:
      return BFD_RELOC_ARC_SDA16_LD2;
    case 11:
      return BFD_RELOC_ARC_SDA16_LD;
    case 12:
      return BFD_RELOC_ARC_SDA16_LD1;
    }

  /* Refer to opcodes/arc-opc.c for 'insn to return value' mappings for this
     function.  */
  switch (ac_get_store_sdasym_insn_type (insn, compact_insn_16))
    {
    case 0:
      return BFD_RELOC_ARC_SDA_LDST2;
    case 1:
      return BFD_RELOC_ARC_SDA_LDST;
    case 2:
      return BFD_RELOC_ARC_SDA_LDST1;
    }

  abort();
}

/* Apply a fixup to the object code.  This is called for all the
   fixups we generated by the call to fix_new_exp, above.  In the call
   above we used a reloc code which was the largest legal reloc code
   plus the operand index.  Here we undo that to recover the operand
   index.  At this point all symbol values should be fully resolved,
   and we attempt to completely resolve the reloc.  If we can not do
   that, we determine the correct reloc code and put it back in the fixup.  */

void
md_apply_fix (fixS *fixP, valueT *valueP, segT seg ATTRIBUTE_UNUSED)
{
  /* In bfd/elf32-arc.c .  */
  extern unsigned long arc_plugin_one_reloc
    (unsigned long, enum elf_arc_reloc_type, int, short *, bfd_boolean);
  char *buf = fixP->fx_where + fixP->fx_frag->fr_literal;
  valueT value;

  /* FIXME FIXME FIXME: The value we are passed in *valueP includes
     the symbol values.  Since we are using BFD_ASSEMBLER, if we are
     doing this relocation the code in write.c is going to call
     bfd_perform_relocation, which is also going to use the symbol
     value.  That means that if the reloc is fully resolved we want to
     use *valueP since bfd_perform_relocation is not being used.
     However, if the reloc is not fully resolved we do not want to use
     *valueP, and must use fx_offset instead.  However, if the reloc
     is PC relative, we do want to use *valueP since it includes the
     result of md_pcrel_from.  This is confusing.  */

  if (fixP->fx_addsy == (symbolS *) NULL)
    {
      value = *valueP;
      fixP->fx_done = 1;
    }
  else if (fixP->fx_pcrel)
    {
      value = *valueP;
    }
  else
    {
      value = fixP->fx_offset;
      if (fixP->fx_subsy != (symbolS *) NULL
	  && fixP->fx_r_type != BFD_RELOC_ARC_TLS_DTPOFF
	  && fixP->fx_r_type != BFD_RELOC_ARC_TLS_DTPOFF_S9
	  && fixP->fx_r_type != BFD_RELOC_ARC_TLS_GD_LD)
	{
	  if (S_GET_SEGMENT (fixP->fx_subsy) == absolute_section)
	    value -= S_GET_VALUE (fixP->fx_subsy);
	  else
	    {
	      /* We can't actually support subtracting a symbol.  */
	      as_bad_where (fixP->fx_file, fixP->fx_line,
			    "expression too complex");
	    }
	}
    }

  if ((int) fixP->fx_r_type >= (int) BFD_RELOC_UNUSED)
    {
      int opindex;
      const struct arc_operand *operand;
      arc_insn insn = 0;

      opindex = (int) fixP->fx_r_type - (int) BFD_RELOC_UNUSED;

      operand = &arc_operands[opindex];

      if (fixP->fx_done)
	{
	  /* Only if the fixup is totally done up is it used
	     correctly. */

	  /* Fetch the instruction, insert the fully resolved operand
	     value, and stuff the instruction back again.  */
	  switch (fixP->fx_size)
	    {
	    case 2:
	      insn = md_chars_to_number (buf, fixP->fx_size);
	      break;
	    case 4:
	      insn = md_chars_to_number (buf, - fixP->fx_size);
	      break;
	    }

	  insn = arc_insert_operand (insn, 0, operand, -1, NULL, (offsetT) value,
				     fixP->fx_file, fixP->fx_line);
	  switch (fixP->fx_size)
	    {
	    case 2:
	      md_number_to_chars (buf, insn, fixP->fx_size);
	      break;
	    case 4:
	      md_number_to_chars (buf, insn, - fixP->fx_size);
	      break;
	    }
	   return;
	}

      /* FIXME:: 19th May 2005 .
	 Is this comment valid any longer with respect
	 to the relocations being in the addends and not in place. We no
	 longer have inplace addends in any case. The only thing valid that
	 needs to be done is to set up the correct BFD reloc values and
	 nothing else.
      */

      /* Determine a BFD reloc value based on the operand information.
	 We are only prepared to turn a few of the operands into relocs.
	 !!! Note that we can't handle limm values here.  Since we're using
	 implicit addends the addend must be inserted into the instruction,
	 however, the opcode insertion routines currently do nothing with
	 limm values.  */
      if (operand->fmt == 'L')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_LIMM) != 0
		  && operand->bits == 32
		  && operand->shift == 32);
	  fixP->fx_r_type = BFD_RELOC_ARC_32_ME;
	}
      /* ARCtangent-A5 21-bit (shift by 2) PC-relative relocation. Used for
	 bl<cc> instruction */
      else if (operand->fmt == 'h')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 19
		  && operand->shift == 18);
	  fixP->fx_r_type = BFD_RELOC_ARC_S21W_PCREL;
	}
      /* ARCtangent-A5 25-bit (shift by 2) PC-relative relocation. Used for
	 'bl' instruction. */
      else if (operand->fmt == 'H')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 23
		  && operand->shift == 18);
	  fixP->fx_r_type = BFD_RELOC_ARC_S25W_PCREL;
	}
      /* ARCtangent-A5 21-bit (shift by 1) PC-relative relocation. Used for
	 'b<cc>' instruction. */
      else if (operand->fmt == 'i')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 20
		  && operand->shift == 17);
	  fixP->fx_r_type = BFD_RELOC_ARC_S21H_PCREL;
	}
      /* ARCtangent-A5 25-bit (shift by 1) PC-relative relocation. Used for
	 unconditional branch ('b') instruction.  */
      else if (operand->fmt == 'I')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 24
		  && operand->shift == 17);
	  fixP->fx_r_type = BFD_RELOC_ARC_S25H_PCREL;
	}
      else if (operand->fmt == 'W')
	{
	  gas_assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 11
		  && operand->shift == 0);
	  fixP->fx_r_type = BFD_RELOC_ARC_S13_PCREL;
	}
      else
	{
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			"unresolved expression that must be resolved");
	  fixP->fx_done = 1;
	}
    }
  else
    {
      /* Zero out the in place addend for relocations */
      if ( !fixP->fx_done)
	value = 0;
      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_8:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 1);
	  break;

	case BFD_RELOC_16:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 2);
	  break;

	case BFD_RELOC_24:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 3);
	  break;

	case BFD_RELOC_32:
	case BFD_RELOC_32_PCREL:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 4);
	  break;

	case BFD_RELOC_ARC_TLS_DTPOFF:
	case BFD_RELOC_ARC_TLS_LE_32:
	  if (fixP->fx_done)
	    {
	      gas_assert (!fixP->fx_addsy);
	      gas_assert (!fixP->fx_subsy);
	      goto apply32_me;
	    }
	  value = fixP->fx_offset;
	  fixP->fx_offset = 0;
	case BFD_RELOC_ARC_TLS_GD_GOT:
	case BFD_RELOC_ARC_TLS_IE_GOT:
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  /* Fall through.  */
	apply32_me:
	case BFD_RELOC_ARC_GOTPC32:
	case BFD_RELOC_ARC_GOTOFF:
	case BFD_RELOC_ARC_32_ME:
	case BFD_RELOC_ARC_PC32:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, -4);

	  break;

	case BFD_RELOC_ARC_TLS_GD_LD:
	  gas_assert (!fixP->fx_offset);
	  if (fixP->fx_subsy)
	    fixP->fx_offset
	      = (S_GET_VALUE (fixP->fx_subsy)
		 - fixP->fx_frag->fr_address- fixP->fx_where);
	  fixP->fx_subsy = NULL;
	  /* Fall through.  */
	case BFD_RELOC_ARC_TLS_GD_CALL:
	  /* These two relocs are there just to allow ld to change the tls
	     model for this symbol, by patching the code.  */
	  /* Fall through.  */
	  /* The offset - and scale, if any - will be installed by the
	     linker.  */
	  gas_assert (!fixP->fx_done);
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  break;
	case BFD_RELOC_ARC_TLS_LE_S9:
	  gas_assert (!fixP->fx_done);
	  /* Fall through.  */
	case BFD_RELOC_ARC_TLS_DTPOFF_S9:
	  if (!fixP->fx_done)
	    S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  else
	    {
	      gas_assert (!fixP->fx_addsy);
	      gas_assert (!fixP->fx_subsy);
	      fixP->fx_offset = value;
	    }
	  short overflow_detected = 0;
	  char *contents = fixP->fx_frag->fr_literal + fixP->fx_where;
	  value = md_chars_to_number (contents, -4);
	  value
	    = arc_plugin_one_reloc (value, R_ARC_TLS_LE_S9, fixP->fx_offset,
				    &overflow_detected, TRUE);
          fixP->fx_offset = 0;
	  goto apply32_me;

	case BFD_RELOC_ARC_B26:
	  /* If !fixP->fx_done then `value' is an implicit addend.
	     We must shift it right by 2 in this case as well because the
	     linker performs the relocation and then adds this in (as opposed
	     to adding this in and then shifting right by 2).  */
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 4);
	  break;
	  /* Take care of PLT relocations for bl<cc>
	     case BFD_RELOC_ARC_PLT25W :
	     break;
	  */

	case BFD_RELOC_ARC_PLT32:
	    /* Currently we are treating PLT32 as a 25bit relocation type */

	    break;

	case BFD_RELOC_ARC_SDA:
	case BFD_RELOC_ARC_SDA32:
	case BFD_RELOC_ARC_SDA_LDST:
	case BFD_RELOC_ARC_SDA_LDST1:
	case BFD_RELOC_ARC_SDA_LDST2:
	case BFD_RELOC_ARC_SDA16_LD:
	case BFD_RELOC_ARC_SDA16_LD1:
	case BFD_RELOC_ARC_SDA16_LD2:
	case BFD_RELOC_ARC_SDA32_ME:
	  break;

	default:
	  abort ();
	}
    }

  return;
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent *
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED,
	      fixS *fixP)
{
  arelent *reloc;
  bfd_reloc_code_real_type code;
  code = fixP->fx_r_type;

  /* FIXME: This should really be BFD_RELOC_32_PCREL, but the linker crashes
     on that.  */
  if (code == BFD_RELOC_32_PCREL)
    code = BFD_RELOC_ARC_PC32;

  if (code == BFD_RELOC_ARC_GOTPC32
      && GOT_symbol
      && fixP->fx_addsy == GOT_symbol)
    code = BFD_RELOC_ARC_GOTPC;

  /* irfan 1 */
  if (fixP->fx_pcrel && fixP->fx_r_type == BFD_RELOC_ARC_32_ME)
    {
      code = BFD_RELOC_ARC_PC32;
      /* When we have a pc-relative value in a LIMM, it's relative to
	 the address of the instruction, not to the address of the LIMM.
	 The linker will just know where to put the relocated value, so
	 make it relative to that address.  */
      fixP->fx_offset
	+= fixP->fx_frag->fr_address + fixP->fx_where - fixP->fx_dot_value;
    }

  reloc = xmalloc (sizeof (arelent));
  reloc->sym_ptr_ptr = xmalloc (sizeof (asymbol *));

  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixP->fx_addsy);
  reloc->address = fixP->fx_frag->fr_address + fixP->fx_where;



  reloc->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (reloc->howto == (reloc_howto_type *) NULL)
    {
      as_bad_where (fixP->fx_file, fixP->fx_line,
		    "internal error: can't export reloc type %d (`%s')",
		    fixP->fx_r_type,
		    bfd_get_reloc_code_name (fixP->fx_r_type));
      return NULL;
    }

  gas_assert (!fixP->fx_pcrel == !reloc->howto->pc_relative);

  if (fixP->fx_r_type == BFD_RELOC_ARC_TLS_DTPOFF
      || fixP->fx_r_type == BFD_RELOC_ARC_TLS_DTPOFF_S9)
    {
      asymbol *sym
	= fixP->fx_subsy ? symbol_get_bfdsym (fixP->fx_subsy) : NULL;
      /* We just want to store a 24 bit index, but we have to wait till
	 after write_contents has been called via bfd_map_over_sections
	 before we can get the index from _bfd_elf_symbol_from_bfd_symbol.
	 Thus, the write_relocs function is elf32-arc.c has to pick up
	 the slack.  Unfortunately, this leads to problems with hosts
	 that have pointers wider than long (bfd_vma).  There would
	 be various ways to handle this, all error-prone :-(  */
      reloc->addend = (bfd_vma) sym;
      if ((asymbol *) reloc->addend != sym)
	{
	  as_bad ("Can't store pointer\n");
	  return NULL;
	}
    }
  else
    reloc->addend = fixP->fx_offset;

  return reloc;
}

const pseudo_typeS md_pseudo_table[] =
{
  { "align", s_align_bytes, 0 }, /* Defaulting is invalid (0).  */
  { "comm", arc_common, 0 },
  { "common", arc_common, 0 },
  { "lcomm", arc_common, 1 },
  { "lcommon", arc_common, 1 },
  { "2byte", cons, 2 },
  { "half", cons, 2 },
  { "short", cons, 2 },
  { "3byte", cons, 3 },
  { "4byte", cons, 4 },
  { "word", cons, 4 },
  { "option", arc_option, 0 },
  { "cpu", arc_option, 0 },
  { "block", s_space, 0 },
  { "extcondcode", arc_extoper, 0 },
  { "extcoreregister", arc_extoper, 1 },
  { "extauxregister", arc_extoper, 2 },
  { "extinstruction", arc_ac_extinst, 0 },
  { "tls_gd_ld",   arc_extra_reloc, BFD_RELOC_ARC_TLS_GD_LD },
  { "tls_gd_call", arc_extra_reloc, BFD_RELOC_ARC_TLS_GD_CALL },
  { NULL, 0, 0 },
};

static struct arc_operand_value num_suf;

static struct arc_operand_value zer_rega={"ZEROV",62,'*',0};
static struct arc_operand_value zer_regb={"ZEROV",62,'(',0};
static struct arc_operand_value zer_regc={"ZEROV",62,')',0};

/* These variables control valid operand prefixes.  */
static const char arc_operand_prefixes[] = "%+";
static const int  arc_operand_prefix_num = 2;


/* Parse enter_s or leave_s arguments. */
static int
parseEnterLeaveMnemonic(char **str,
			bfd_boolean leave_p)
{
  int value = -1;
  char *old_input_line_pointer;
  bfd_boolean saw_comma = FALSE;
  bfd_boolean saw_arg = FALSE;
  bfd_boolean saw_fp = FALSE;
  bfd_boolean saw_dash = FALSE;
  bfd_boolean saw_r13 = FALSE;
  bfd_boolean saw_blink = FALSE;
  bfd_boolean saw_pcl = FALSE;
  unsigned char regVal = 0;

  /* Save and restore input_line_pointer around this function.  */
  old_input_line_pointer = input_line_pointer;
  input_line_pointer = *str;

  while (*input_line_pointer)
    {
      SKIP_WHITESPACE();
      switch (*input_line_pointer)
        {
	case '}':
        case '\0':
          goto fini;

        case ',':
          input_line_pointer++;
          if (saw_comma || !saw_arg)
            goto err;
          saw_comma = TRUE;
	  saw_arg = FALSE;
          break;

	case '-':
	  if (saw_dash)
	    goto err;
	  saw_dash = TRUE;

	case '%':
	  input_line_pointer++;
	  break;

	case 'f':
	  if (saw_arg && !saw_comma)
	    goto err;

	  if (saw_fp)
	    goto err;

	  if (strncmp (input_line_pointer, "fp", 2))
	    goto err;

	  input_line_pointer +=2;
	  saw_fp    = TRUE;
	  saw_comma = FALSE;
	  saw_arg   = TRUE;
	  break;

	case 'b':
	  if (saw_arg && !saw_comma)
	    goto err;

	  if (strncmp (input_line_pointer, "blink", 5))
	    goto err;

	  if (saw_blink)
	    goto err;

	  input_line_pointer +=5;
	  saw_blink = TRUE;
	  saw_comma = FALSE;
	  saw_arg   = TRUE;
	  break;

	case 'p':
	  if (saw_arg && !saw_comma)
	    goto err;

	  if (!leave_p)
	    goto err;

	  if (strncmp (input_line_pointer, "pcl", 3))
	    goto err;

	  if (saw_pcl)
	    goto err;

	  input_line_pointer +=3;
	  saw_pcl   = TRUE;
	  saw_comma = FALSE;
	  saw_arg   = TRUE;
	  break;

	case 'r':
          if (saw_arg && !(saw_comma ^ saw_dash))
	    goto err;

	  if (!saw_r13 && strncmp (input_line_pointer, "r13", 3))
	    goto err;

	  if (saw_r13 && !saw_dash)
	    goto err;

	  input_line_pointer += (saw_r13) ? 1 : 3;
	  if (saw_r13)
	    {
	      /* Expect r13 to r26 */
	      unsigned char v0 = *input_line_pointer++;
	      unsigned char v1 = *input_line_pointer++;
	      if ((v0 < '1' || v0 > '2') || (v1 < '0' || v1 >'9'))
		{
		  as_bad (_("register out of range"));
		  goto err;
		}
	      regVal = (v0 - '0')*10 + (v1 - '0');
	    }
	  else
	    {
	      regVal = 13;
	    }

	  saw_r13   = TRUE;
	  saw_comma = FALSE;
	  saw_arg   = TRUE;
	  break;

	default:
	  goto err;
	}
    }

 fini:
  if (saw_comma)
    goto err;
  if (saw_r13 && (regVal > 26 || regVal < 13))
    goto err;
  *str = input_line_pointer;
  input_line_pointer = old_input_line_pointer;

  value = (saw_r13) ? regVal - 12 : 0x00;
  value |= (saw_fp) ? 0x10 : 0x00;
  value |= (saw_blink) ? 0x20 : 0x00;
  value |= (saw_pcl && leave_p) ? 0x40 : 0x00;

  return value;

 err:
  if (saw_comma)
    as_bad (_("extra comma"));
  else if (!saw_arg)
    as_bad (_("missing argument"));
  else
    as_bad (_("wrong input"));
  input_line_pointer = old_input_line_pointer;
  return -1;

}

/* Verify that we may use pic; warn if we may not.  */

static bfd_boolean
assert_arc_pic_support (void)
{
  if (arc_mach_type == bfd_mach_arc_arc700
      || arc_mach_type == bfd_mach_arc_arcv2)
    return TRUE;
  as_warn (_("PIC not supported for processors prior to ARC 700\n"));
  return FALSE;
}

/* This routine is called for each instruction to be assembled.  */

void
md_assemble (char *str)
{
  const struct arc_opcode *opcode;
  const struct arc_opcode *std_opcode;
  struct arc_opcode *ext_opcode;
  char *start, *s;
  char *firstsuf;
  const char *last_errmsg = 0;
  int lm_present;
  arc_insn insn;
  long insn2;
  long convert_scaled;
  static int init_tables_p = 0;
  current_special_sym_flag = NO_TYPE;
  char insn_name[64]={0};
  unsigned int insn_name_idx = 0;
  /* Non-zero if the insn being encoded is 16-bit ARCompact instruction */
  int compact_insn_16;
  /* Index into arc_operand_prefixes array */
  unsigned char opindex = 0;

  assembling_instruction = 1;

  /* Opcode table initialization is deferred until here because we have to
     wait for a possible .option command.  */
  if (!init_tables_p)
    {
      init_opcode_tables (arc_mach_type);
      init_tables_p = 1;
    }

  /* Skip leading white space.  */
  while (ISSPACE (*str))
    str++;

  /* Check whether insn being encoded is 16-bit ARCompact insn */
  for (s = str; (*s && (ISALNUM (*s) ) ) ; s++)
  {;}


  for (insn_name_idx = 0; insn_name_idx < (unsigned int) strlen(str); insn_name_idx++)
  {
	if ( !(ISALNUM(str[insn_name_idx]) || str[insn_name_idx] == '_') ){
		break;
	}
	insn_name[insn_name_idx] = str[insn_name_idx];
  }

  /* All ARCompact 16 bit instructions have a <operation_name>_s which
   * is what we attempt to exploit here .
   */
  if ((*s && *s == '_' && *(s+1) == 's') || strcmp(str,"unimp") == 0) /* FIXME: cleanup required */
    compact_insn_16 = 1;
  else
    compact_insn_16 = 0;

  /* The instructions are stored in lists hashed by the first letter (though
     we needn't care how they're hashed).  Get the first in the list.  */

  ext_opcode = arc_ext_opcodes;
  std_opcode = arc_opcode_lookup_asm (str);
#if DEBUG_INST_PATTERN
fprintf (stdout, "Matching ****** %s *************\n", str);
#endif
  /* Keep looking until we find a match.  */
  start = str;
  for (opcode = (ext_opcode ? ext_opcode : std_opcode);
       opcode != NULL;
       opcode = (ARC_OPCODE_NEXT_ASM (opcode)
		 ? ARC_OPCODE_NEXT_ASM (opcode)
		 : (ext_opcode ? ext_opcode = NULL, std_opcode : NULL)))
    {
      int past_opcode_p, fc, num_suffixes;
      int fix_up_at = 0;
      int fix_up4_bbitbr = 0;
      unsigned char *syn;
      struct arc_fixup fixups[MAX_FIXUPS];
      int mods=0;

      /* Used as a sanity check.  If we need a limm reloc, make sure we ask
	 for an extra 4 bytes from frag_more.  */
      int limm_reloc_p;
      int ext_suffix_p;
      const struct arc_operand_value *insn_suffixes[MAX_SUFFIXES];
      int regb_p;
      const struct arc_operand_value *regb;
      const struct arc_operand_value *regh;

      /* Is this opcode supported by the selected cpu?  */
      if (!arc_opcode_supported (opcode))
	continue;

      /* If opcode syntax is for 32-bit insn but input is 16-bit insn,
	 then go for the next opcode */
      for (syn = opcode->syntax; *syn && ISALNUM (*syn); syn++);
      if (compact_insn_16 && !(*syn && *syn == '_' && *(syn + 1) == 's'))
	if (strcmp((char *) (opcode->syntax), "unimp") !=0) /* FIXME: This is too bad a check!!! cleanup required */
	  continue;

      /* Scan the syntax string.  If it doesn't match, try the next one.  */
      arc_opcode_init_insert ();
      insn = opcode->value;
      insn2 = opcode->value2;
      convert_scaled = 0;
      lm_present = 0;
      fc = 0;
      num_suf.value = 0;
      firstsuf = 0;
      past_opcode_p = 0;
      num_suffixes = 0;
      limm_reloc_p = 0;
      ext_suffix_p = 0;
      regb_p = 0;
      regb = NULL;
      regh = NULL;
#if DEBUG_INST_PATTERN
fprintf (stdout, "Trying syntax %s\n", opcode->syntax);
#endif
      /* We don't check for (*str != '\0') here because we want to parse
	 any trailing fake arguments in the syntax string.  */
      for (str = start, syn = opcode->syntax; *syn != '\0';)
	{
	  const struct arc_operand *operand;

#if DEBUG_INST_PATTERN
printf(" syn=%s str=||%s||insn=%x\n",syn,str,insn);//ejm
#endif
	  for(opindex = 0; opindex < arc_operand_prefix_num; opindex++) {
		if (*syn == arc_operand_prefixes[opindex])
			break;
	  }
	  /* Non operand chars must match exactly.  */
	    if (*syn != arc_operand_prefixes[opindex] || *++syn == arc_operand_prefixes[opindex])
	      {
		if (*str == *syn || (*syn=='.'&&*str=='!'))
		  {
		  if (*syn == ' '){
		      past_opcode_p = 1;
		      }
		    ++syn;
		    ++str;
		  }
		else
		  break;
		continue;
	      }
	  if (firstsuf==0)
	    firstsuf = (char *) (syn-1);
	  /* We have an operand.  Pick out any modifiers.  */
	  mods = 0;
	  while (ARC_MOD_P (arc_operands[arc_operand_map[(int) ((opindex<<8)|*syn)]].flags))
	    {
	      if (arc_operands[arc_operand_map[(int) ((opindex<<8)|*syn)]].insert)
#if 1
		/* FIXME: Need 'operand' parameter which is uninitialized.  */
		abort ();
#else
		(arc_operands[arc_operand_map[(int) *syn]].insert) (insn, operand, mods, NULL, 0, NULL);
#endif

	      mods |= (arc_operands[arc_operand_map[(int) ((opindex<<8)|*syn)]].flags
		       & ARC_MOD_BITS);
	      ++syn;
	    } /* end while(ARC_MOD_P(...)) */
	  operand = arc_operands + arc_operand_map[(int) ((opindex<<8)|*syn)];
	  if (operand->fmt == 0){
	    as_fatal ("unknown syntax format characters '%c%c'", arc_operand_prefixes[opindex], *syn);
	      }

	  if (operand->flags & ARC_OPERAND_FAKE)
	    {
	      const char *errmsg = NULL;
	      if (operand->insert)
		{
		  insn = (*operand->insert) (insn,&insn2, operand, mods, NULL, 0,
					     &errmsg);
		  if (errmsg != (const char *) NULL)
		    {
		      last_errmsg = errmsg;
		      if (operand->flags & ARC_OPERAND_ERROR)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_bad (errmsg, "");
			  assembling_instruction = 0;
			  return;
			}
		      else if (operand->flags & ARC_OPERAND_WARN)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_warn (errmsg, "");
			}
		      break;
		    }
		  if (limm_reloc_p
		      && (operand->flags && operand->flags & ARC_OPERAND_LIMM)
		      && (operand->flags
			  & (ARC_OPERAND_ABSOLUTE_BRANCH
			     | ARC_OPERAND_ADDRESS)))
		    {
		      fixups[fix_up_at].opindex = arc_operand_map[operand->fmt];
		    }
		  /*FIXME! This is a hack for STAR9000593624: there
		    are cases when an st or ld instruction needs a
		    shimm fixup. When the fixup is created, the
		    limm_reloc_p is also setup, we need to turn it
		    off. Hence we do it so at the end of the ld or st
		    instruction. This is not the way of handling
		    fixups.*/
		  if (limm_reloc_p
		      && (operand->fmt == '1' || operand->fmt == '0'))
		    {
		      limm_reloc_p = 0;
		    }
		  /*ARCv2: Value of bit Y for branch prediction can be
		    done only when we know the displacement. Hence, I
		    override the 'd' modchar fixup with an insert
		    function that depends on the mnemonic <.T> flag*/
		  if (arc_get_branch_prediction() &&
		      (operand->fmt == 144 || operand->fmt == 145))
		    {
		      if (operand->fmt == 144)
			{
			  /*BBIT*/
			  fixups[fix_up4_bbitbr].opindex =
			    arc_operand_map[operand->fmt + arc_get_branch_prediction() - 1];
			}
		      if (operand->fmt == 145)
			{
			  /*BR*/
			  fixups[fix_up4_bbitbr].opindex =
			    arc_operand_map[operand->fmt - arc_get_branch_prediction() + 1];
			}
		      arc_reset_branch_prediction();
		    }
		}
	      ++syn;
	    }
	  /* Are we finished with suffixes?  */
	  else if (!past_opcode_p)
	    {
	      int found,negflg;
	      char c;
	      char /* *s, */ *t;	/* Can reuse "s" from above */
	      const struct arc_operand_value *suf, *suffix_end;
	      struct arc_operand_value *varsuf;
	      const struct arc_operand_value *suffix = NULL;

	      if (!(operand->flags & ARC_OPERAND_SUFFIX)){
		abort ();
		  }


	      /* If we're at a space in the input string, we want to skip the
		 remaining suffixes.  There may be some fake ones though, so
		 just go on to try the next one.  */
	      if (*str == ' ')
		{
		  ++syn;
		  continue;
		}


	      s = str;
	      negflg = 0;
	      if (mods & ARC_MOD_DOT)
		{
		negflg = *s=='!';
		  if (*s != '.'&&*s != '!')
		    break;
		  ++s;
		if(!negflg && *s == '!'){
		    negflg = 1;
		    ++s;
		    }
		}
	      else
		{
		  /* This can happen in "b.nd foo" and we're currently looking
		     for "%q" (ie: a condition code suffix).  */
		  if (*s == '.')
		    {
		      ++syn;
		      continue;
		    }
		}
	      /* Pick the suffix out and look it up via the hash table.  */
	      for (t = s; *t && ISALNUM (*t); ++t)
		continue;
	      c = *t;
	      *t = '\0';
	      found = 0;
	      suf = NULL;
	      if(!found && ((insn >> 27) == 0x0a)){
		  char *restore;
		  int sum=0;
		  if(num_suf.type == 0){
		      int i;
		      for(i=0;i<256;i++){
			  if(arc_operands[i].fmt == ']'){
			      num_suf.type = i;
			      break;
			      }
			  if(arc_operands[i].fmt == 0)break;
			  }
		      } /* end if(num_suf.type == 0) */

		  if(*syn == ']' || *(syn+3) == ']'){
		      restore = str;
		      if(*str == '.' || *str == '!')str++;
		      if(*str == '.' || *str == '!')str++;
		      if((*str == 'i' || *str == 'I') && (*(str+1) >= '0'
				 && *(str+1)<='9')){
			  str++;
			  sum = 0;
			      if(*str  ==  '1'){
				  sum = 1;
				  str++;
			      }
			      if(*str >= '0' && *str <= '9'){
				  sum = sum*10 + *str-'0';
				  str++;
				  }
			      sum = sum & 0xf;
			      if(negflg)
				  sum |= 0x40; //negation flag
			      suf = &num_suf;
			      varsuf = &num_suf;
			      varsuf->value = sum;
			      insn2 |= sum << 15;
			      insn2 |= 1 << 29;
			      lm_present = 1;
			      if(firstsuf)
				syn = (unsigned char *) (firstsuf-1);
			      found = 1;
			  }
		      else
			  {
			  if(*str == '0' && *(str+1) == 'x'){
			      str = str+2;
			      while(1){
				  if(*str >= '0' && *str <= '9')
				      {
				      sum = (sum << 4) + *str-'0';
				      str++;
				      }
				  else {
				      if(*str >= 'a' && *str <= 'z'){
					  sum = (sum <<4) + *str-'a'+10;
					  str++;
					  }
				      else
					  break;
				      }
				  } /* end while(1) */
			      suf = &num_suf;
			      varsuf = &num_suf;
			      /* lane masks accumulate */
			      varsuf->value |= sum;
			      found = 1;
			      if(firstsuf)
				syn = (unsigned char *) (firstsuf-1);
			      insn2 |= sum << 15;
			      lm_present = 1;
			      }
			  else
			      {
			      if(*(str) >= '0' && *(str) <= '9'){
				  while(*str >= '0' && *str <= '9'){
				      sum = sum*10 + *str-'0';
				      str++;
				      }
				  suf = &num_suf;
				  varsuf = &num_suf;
				  /* lane masks accumulate */
				  varsuf->value |= sum;
				  found = 1;
				  if(firstsuf)
				    syn = (unsigned char *) (firstsuf-1);
				  insn2 |= sum << 15;
				  lm_present = 1;
				  }
			      else
				  {
				  if(*str == 'u'){
				      str++;
				      if(*str == 's' || *str == 'S')str++;
				      found = 1;
				      sum = 0x20;
				      insn2 |= sum << 23;
				      lm_present = 1;
				      suf=&num_suf;
				      }
				  if((*str == 's' || *str == 'S') && found == 0){
				      found = 1;
				      str++;
				      sum = 0x14;
				      insn2 |= sum << 23;
				      lm_present = 1;
				      suf = &num_suf;
				      }
				  if((*str == 'l' || *str == 'L') && found == 0){
				      found = 1;
				      str++;
				      sum = 0xc;
				      if(*str == 'e' || *str == 'E'){
					  str++;
					  sum = 0xc;
					  }
				      if(*str == 's' || *str == 'S'){
					  str++;
					  sum = 0xf;
					  }
				      if(*str == 't' || *str == 'T'){
					  str++;
					  sum = 0xb;
					  }
				      if(*str == 'o' || *str == 'O'){
					  str++;
					  sum = 0x5;
					  }
				      insn2 |= sum << 23;
				      lm_present = 1;
				      suf = &num_suf;
				      }
				  if((*str == 'g' || *str == 'G') && found==0){
				      found = 1;
				      str++;
				      sum = 0xa;
				      if(*str == 'e' || *str == 'E'){
					  str++;
					  sum = 0xa;
					  }
				      if(*str == 't' || *str == 'T'){
					  str++;
					  sum = 0x9;
					  }
				      insn2 |= sum << 23;
				      suf = &num_suf;
				      lm_present = 1;
				      }
				  if((*str == 'h' || *str == 'H') && found==0){
				      found = 1;
				      str++;
				      sum = 0xd;
				      if(*str == 'i' || *str == 'I'){
					  str++;
					  sum = 0xd;
					  }
				      if(*str == 's' || *str == 'S'){
					  str++;
					  sum = 0x6;
					  }
				      insn2 |= sum << 23;
				      lm_present = 1;
				      suf = &num_suf;
				      }
				  if((*str == 'z' || *str == 'Z') && found == 0){
				      str++;
				      insn2 |= 1 << 23;
				      found = 1;
				      lm_present = 1;
				      suf = &num_suf;
				      }
				  }
			      if((*str == 'e' || *str == 'E') && found == 0){
				  str++;
				  if(*str == 'q') str++;
				  insn2 |= 1 << 23;
				  lm_present = 1;
				  found = 1;
				  suf = &num_suf;
				  }
			      if((*str == 'f' || *str == 'F') && found == 0){
				  str++;
				  insn |= 1 << 15;
				  found = 1;
				  suf = &num_suf;
				  lm_present = 1;
				  }
			      if((*str == 'n' || *str == 'N') && found == 0){
				  str++;
				  sum = 2;
				  if(*str == 'z' || *str == 'Z'){
				      str++;
				      sum = 2;
				      }
				  if(*str == 'e' || *str == 'E'){
				      str++;
				      sum = 2;
				      }
				  if(*str == 'c' || *str == 'C'){
				      str++;
				      sum = 6;
				      }
				  insn2 |= sum << 23;
				  found = 1;
				  lm_present = 1;
				  suf = &num_suf;
				  }
			      if((*str == 'c' || *str == 'C') && found == 0){
				  str++;
				  if(*str == 'c' || *str == 'C')str++;
				  sum = 6;
				  found = 1;
				  insn2 |= sum << 23;
				  suf = &num_suf;
				  lm_present = 1;
				  }
			      if(!found){
				  str = restore;
				  }
			      }
			  }
		      }
		  } /* end if(!found&&insn>>27==0x0a) */
	      if(!suf){
		  if ((suf = get_ext_suffix (s,*syn))){
		      ext_suffix_p = 1;
		      }
		  else
		      {
		      suf = hash_find (arc_suffix_hash, s);
		      }
		  }

	      if (!suf)
		{
		  /* This can happen in "blle foo" and we're currently using
		     the template "b%q%.n %j".  The "bl" insn occurs later in
		     the table so "lle" isn't an illegal suffix.  */
		  *t = c;
		  break;
		}
	      /* Is it the right type?  Note that the same character is used
		 several times, so we have to examine all of them.  This is
		 relatively efficient as equivalent entries are kept
		 together.  If it's not the right type, don't increment `str'
		 so we try the next one in the series.  */
	      if (ext_suffix_p && arc_operands[suf->type].fmt == *syn)
		{
		  /* Insert the suffix's value into the insn.  */
		  *t = c;
		  if (operand->insert)
		    insn = (*operand->insert) (insn,&insn2, operand,
					       mods, NULL, suf->value,
					       NULL);
		  else
		    insn |= suf->value << operand->shift;
		  suffix = suf;
		  str = t;
		  found = 1;
		}
	      else
		{
		  *t = c;
		  suffix_end = arc_suffixes + arc_suffixes_count;
		  for (suffix = suf;
		       (suffix < suffix_end
			&& strcmp (suffix->name, suf->name) == 0);
		       ++suffix)
		    {
		      if (arc_operands[suffix->type].fmt == *syn)
			{
			  /* Insert the suffix's value into the insn.  */
			  if (operand->insert)
			    insn = (*operand->insert) (insn,&insn2, operand, mods,
						       NULL, suffix->value,
						       NULL);
			  else
			    insn |= suffix->value << operand->shift;

			  str = t;
			  found = 1;
			  break;
			} /* end if(arc_operands[suffix->type].fmt == *syn) */
		    } /* end for(suffix=suf; ....) */
		}
	      ++syn;
	      if (!found)
		/* Wrong type.  Just go on to try next insn entry.  */
		;
	      else
		{
		  if (num_suffixes == MAX_SUFFIXES)
		    as_bad ("too many suffixes");
		  else
		    insn_suffixes[num_suffixes++] = suffix;
		}
	    }
	  else if ((operand->fmt == 135) || (operand->fmt == 138))
	    {
	      /* enter or leave mnemonic, parse the string and
		 compute the value. */

	      bfd_boolean leave_p = (operand->fmt == 138) ? TRUE : FALSE;
	      int value = parseEnterLeaveMnemonic(&str, leave_p);
	      if (value == -1)
		break;
	      syn++;

	      /* Insert the register or expression into the instruction.  */
	      if (operand->insert)
		{
		  const char *errmsg = NULL;
		  insn = (*operand->insert) (insn, NULL, operand, 0,
					     NULL, (long) value, &errmsg);
		  if (errmsg != (const char *) NULL)
		    {
		      last_errmsg = errmsg;
		      if (operand->flags & ARC_OPERAND_ERROR)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_bad (errmsg, "");
			  assembling_instruction = 0;
			  return;
			}
		      else if (operand->flags & ARC_OPERAND_WARN)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_warn (errmsg, "");
			}
		      break;
		    }
		}
	    }
	  else
	    /* This is either a register or an expression of some kind.  */
	    {
	      char *hold;
	      const struct arc_operand_value *reg = NULL;
	      int match_failed = 0;
	      const char *match_str;
	      long value = 0;
	      expressionS exp;
	      exp.X_op = O_illegal;
	      if (operand->flags & ARC_OPERAND_SUFFIX)
		abort ();

	      /* Is there anything left to parse?
		 We don't check for this at the top because we want to parse
		 any trailing fake arguments in the syntax string.  */
	      if (is_end_of_line[(unsigned char) *str])
		break;

	      /* Verify the input for the special operands for ARCompact ISA */
	      switch (operand->fmt)
		{
		case '4': match_str = "r0"; break;
		case '5': match_str = "gp"; break;
		case '6': match_str = "sp"; break;
		case '7': match_str = "ilink1"; break;
		case '8': match_str = "ilink2"; break;
		case '9': match_str = "blink"; break;
		case '!': match_str = "pcl"; break;
		/*ARCv2 special registers*/
		case 129: match_str = "r1"; break;
		case 130: match_str = "r2"; break;
		case 131: match_str = "r3"; break;
		default: match_str = NULL; break;
		}
	      if (match_str)
		{
		  int len = strlen (match_str);
		  if (*str == '%')
		    str++;
		  if (strncmp (str, match_str, len))
		    match_failed = 1;
		  else if (ISALNUM (*(str + len)))
		    match_failed = 1;
		  if (match_failed)
		    break;
		}

	      {
		/* Parse the operand.  */
		/* If there is any PIC / small data / etc. related suffix
		   (@gotpc / @gotoff / @plt / @h30 / @sda), after calling
		   expression, input_line_pointer will point to that suffix.  */

		hold = input_line_pointer;

		input_line_pointer = str;
		expression (&exp);

		str = input_line_pointer;
		input_line_pointer = hold;
	      }

	      if (exp.X_op == O_illegal)
		as_bad ("illegal operand");
	      else if (exp.X_op == O_absent)
		as_bad ("missing operand");
	      else if (exp.X_op == O_constant)
		{
		  value = exp.X_add_number;
		  /* Ensure that the constant value is within the
		     operand's limit, for ARCompact ISA */
		  /* FIXME: clean up.  */
		  if (1)
		    {
		      /* Try next insn syntax, if the current operand being
			 matched is not a constant operand */
		      if (!ac_constant_operand (operand))
			break;
		      switch (operand->fmt)
			{
			case 'u':
			  if (opcode->flags & ARC_INCR_U6)
			    value++;    /* Incrementing value of u6 for pseudo
					   mnemonics of BRcc .  */
			case '@':
			  if ((value < 0) || (value > 63))
			    {
			      match_failed = 1;
			    }
			  break;
			case 'K':
			  if ((value < -2048) || (value > 2047))
			    match_failed = 1;
			  break;
			case 'o':
			    if ((value < -256) || (value > 255)){
			    match_failed = 1;
/* Test for instruction convertable to scaled load/store instruction
 * This is necessary to mesh with MetaWare which automagically converts
 * suitable unscaled signed nine bit load/stores that are out of range to
 * in range scaled loads/stores.
 * If the scaled/writeback field mask 0x18 for store and 0x600 for load is
 * zero the instruction is suitable for conversion.
 * No conversion is done if the address constant is in range to begin with.
 * No conversion is done if the short constant is not even or the long
 *  constant is not a multiple of four.
 */

/* test and convert load */
				if( (insn >> 27) == 0x2
				    && ((insn & 0x600) >> 9) == 0
				    && arc_test_wb() == 0)
				    {
				    if(((insn>>7)&3) == 2 && (value & 1) == 0
				       && value >= -512 && value <= 511)
					{
					match_failed = 0;
					convert_scaled = 0x600;
					value = value >> 1;
					}
				    if(((insn >> 7) & 3) == 0
				       && (value & 3) == 0  && value >=-1024
				       && value <=1023 && arc_test_wb()==0)
					{
					match_failed = 0;
					convert_scaled = 0x600;
					value = value >>2;
					}
				    }
/* test and convert store */
				if( (insn>>27) == 0x3
				    && ((insn & 0x18)>>3) == 0
				    && arc_test_wb() == 0)
				    {
				    if(((insn>>1)&3) == 2 && (value&1) == 0
				       && value >=-512 && value <= 511 )
					{
					match_failed = 0;
					convert_scaled = 0x18;
					value = value >> 1;
					}
				    if(((insn>>1)&3) == 0 && (value&3) == 0
				       && value >=-1024 && value <= 1023)
					{
					match_failed = 0;
					convert_scaled = 0x18;
					value = value >> 2;
					}
				    } /* end if((insn>>27)==0x3 &&...) */
				} /* end if( (value<-256) || (value>255) )*/
			  break;
			case 'e':
			  if ((value < 0) || (value > 7))
			    match_failed = 1;
			  break;
			case 'E':
			  if ((value < 0) || (value > 31))
			    match_failed = 1;
			  break;
			case 'j':
			  if ((value < 0) || (value > 127))
			    match_failed = 1;
			  break;
			case 'J':
			  if ((value < 0) || (value > 255))
			    match_failed = 1;
			  break;
			case 'k':
			  if ((value % 2) || (value < 0) || (value > 63))
			    match_failed = 1;
			  break;
			case 'l':
			  if ((value % 4) || (value < 0) || (value > 127))
			    match_failed = 1;
			  break;
			case 'm':
			  if ((value % 4) || (value < 0) || (value > 1023))
			    match_failed = 1;
			  break;
			case 'M':
			  if ((value < -256) || (value > 255))
			    match_failed = 1;
			  break;
			case 'O':
			  if ((value % 2))
			    as_warn ("The constant must be 2-byte aligned");
			  if ((value % 2) || (value < -512) || (value > 511))
			    match_failed = 1;
			  break;
			case 'R':
			  if ((value % 4) || (value < -1024) || (value > 1023))
			    match_failed = 1;
			  break;
			case '\24':
			    if((value > 0x3fff) || (value <-(0x3fff)))
				match_failed = 1;
			    break;
			case '\25':
			    if(value !=0)
				match_failed = 1;
			    break;
			case '\20':
			case '\23': /* discarded constant field */
			    break;
			case '\21':
			case '\22':
			    if(value<0||value >0xff)
				match_failed = 1;
			    break;
			case '\14': /* signed 12 bit operand */
			    switch(opcode->flags&(ARC_SIMD_SCALE1
					   | ARC_SIMD_SCALE2
					   | ARC_SIMD_SCALE3
					   | ARC_SIMD_SCALE4)){
			    case ARC_SIMD_SCALE1:
				if((value&0x1)!=0)
				    as_warn("Offset must be divisible by 2.");
				value = value>>1;
				if((value>2047)||(value<-2048))
				    match_failed = 1;
				break;
			    case ARC_SIMD_SCALE2:
				if((value&0x3)!=0)
				    as_warn("Offset must be divisible by 4.");
				value = value>>2;
				if((value>2047)||(value<-2048))
				    match_failed = 1;
				break;
			    case ARC_SIMD_SCALE3:
				if((value&0x7)!=0)
				    as_warn("Offset must be divisible by 8.");
				value = value>>3;
				if((value>2047)||(value<-2048))
				    match_failed = 1;
				break;
			    case ARC_SIMD_SCALE4:
				if((value&0xf)!=0)
				    as_warn("Offset must be divisible by 16.");
				value = value>>4;
				if((value>2047)||(value<-2048))
				    match_failed = 1;
				break;
			    default:;
				break;
			    } /* end switch(opcode->flags&&(...)) */
			    break;
			case '?':       /* SIMD Unsigned 8 bit operand */
			  switch (opcode->flags & (ARC_SIMD_SCALE1
						   | ARC_SIMD_SCALE2
						   | ARC_SIMD_SCALE3
						   | ARC_SIMD_SCALE4))
			    {
			    case ARC_SIMD_SCALE1:
			      if (value != ((value >> 1) << 1))
				as_warn ("Offset must be divisible by 2. Truncating last bit ");
			      value = value >> 1;
			      break;

			    case ARC_SIMD_SCALE2:
			      if (value != ((value >> 2) << 2))
				as_warn ("Offset must be divisible by 4. Truncating last 2 bits ");
			      value = value >> 2;
			      break;
			    case ARC_SIMD_SCALE3:
			      if (value != ((value >> 3) << 3))
				as_warn ("Offset must be divisible by 8. Truncating last 3 bits ");
			      value = value >> 3;
			      break;
			    case ARC_SIMD_SCALE4:
			      if (value != ((value >> 4) << 4))
				as_warn ("Offset must be divisible by 16. Truncating last 4 bits ");
			      value = value >> 4;
			      break;
			    default:
			      ;
			    } /* end switch (opcode->flags&&(ARC_SIMD_SCALE1...))*/
/* for compatibility with corner cases of MetaWare assembler allow to -128 */
			  if ((value < 0) || (value > 255)){
			    match_failed = 1;
			      }
			  break;
			case 'd': /* 9bit signed immediate, used by bbit*/
			  if (value % 2)
			    {
			      as_warn ("The constant must be 2-byte aligned");
			      match_failed = 1;
			    }
			  if ((value > 255) || (value < -256))
			    {
			      match_failed = 1;
			    }
			  break;
			  /* Not very nice: check constants for ARCv2*/
			case 'L':
			  break;
			case 132: /*w6 6bit signed*/
			  if ((value > 31 || value < -32))
			    {
			      match_failed = 1;
			    }
			  break;
			case 133: /*s3 3bit signed*/
			  if ((value > 6 || value < -1))
			    {
			      match_failed = 1;
			    }
			  break;
			case 134: /*u6 6bit unsigned as used in ADD_S*/
			case 135: /*u6 as used in ENTER_S*/
			  if ((value > 63 || value < 0))
			    {
			      match_failed = 1;
			    }
			  break;
			case 136: /* u10*/
			  if ((value > 0x3FF || value < 0))
			    {
			      match_failed = 1;
			    }
			  break;
			case 138: /*u7 as is leave_s*/
			case 137: /*u7 as in ldi_s*/
			  if ((value > 0x7F || value < 0))
			    {
			      match_failed = 1;
			    }
			  break;
			case 141: /*s11 as in st_s*/
			  if (value % 4)
			    {
			      as_warn ("The constant must be 4-byte aligned");
			      match_failed = 1;
			    }
			  if((value < -1024) || (value > 1023))
			    {
			      match_failed = 1;
			    }
			  break;
			case 142: /*u5 as in ld_s*/
			  if (value % 4)
			    {
			      as_warn ("The constant must be 4-byte aligned");
			      match_failed = 1;
			    }
			  if((value < 0) || (value > 63))
			    {
			      match_failed = 1;
			    }
			  break;
			default:
			  as_warn ("Unchecked constant");
			} /* end switch(operand->fmt) */

		      if (match_failed)
			break;
		    } /* end if (1) */
		} /* else if(exp.X_op==O_constant ) */

	      /* Try next insn syntax if the input operand
		 is a symbol but the current operand being matched is not a
		 symbol operand */
	      else if (exp.X_op == O_symbol && !ac_symbol_operand (operand))
		{
		  break;
		}

	      /* Try next insn syntax if "%st" operand is
		 not being matched with long-immediate operand */
	      /* FIXME: just as bad (and essentially the same) as the next
		 hack, even though triggered less often.  */
	      else if (exp.X_op == O_right_shift && operand->fmt != 'L')
		break;
	      /* FIXME: This is an atrocious hack.
		 We reject add insn variants for forward references if they
		 can't accomodate a LIMM.  We should instead use a variable
		 size fragment and relax this insn.  */
	      else if (exp.X_op != O_register && operand->fmt != 'L'
		       && ( (insn_name[0] == 'a' || insn_name[0] == 'A') &&
			    (insn_name[1] == 'd' || insn_name[1] == 'D') &&
			    (insn_name[2] == 'd' || insn_name[2] == 'D') ) )
		{
		  break;
		}
	      else if (exp.X_op == O_register)
		{
		  reg = (struct arc_operand_value *) exp.X_add_number;
		  /* FIXME: clean up.  */
		  if (1) /* For ARCompact ISA */
		    {
		      /* Try next instruction syntax, if the current operand
			 being matched is not a register operand. */

		      if (!ac_register_operand (operand)
			  && !ARC700_register_simd_operand (operand->fmt))
			break;

		      /* For 16-bit insns, select proper register value */
		      if (compact_insn_16
			  && ((operand->fmt == 'a')
			      || (operand->fmt == 'b')
			      || (operand->fmt == 'c')))
			{
			  int i, l;
			  for (i = 0; i < arc_reg_names_count; i++)
			    {
			      if (!arc_opval_supported (&arc_reg_names[i]))
				continue;
			      l = strlen (arc_reg_names[i].name);
			      if ((arc_reg_names[i].flags & ARC_REGISTER_16)
				  && !strncmp (reg->name,
					       arc_reg_names[i].name, l)
				  && !ISALNUM (*(reg->name + l)))
				{
				  reg = &arc_reg_names[i];
				  break;
				}
			    } /* end for(i=0;i<arc_reg_names_count; i++ ) */
			  if (i == arc_reg_names_count)
			    break;
			} /* end if(compact_insn_16...) */

		      /* Ashwin: For SIMD instructions checking if its any
			 of the SIMD register.*/
		      if (ARC700_register_simd_operand (operand->fmt)
			  && !ac_register_operand (operand))
			{

			  struct arc_ext_operand_value *ext_oper
			    = arc_ext_operands;

			  while (ext_oper)
			    {
			      short flg = 0;

			      switch (ext_oper->operand.flags
				      & (ARC_REGISTER_SIMD_VR
					 | ARC_REGISTER_SIMD_I
					 | ARC_REGISTER_SIMD_K
					 | ARC_REGISTER_SIMD_DR))
				{
				case ARC_REGISTER_SIMD_VR:
				  if ((ARC700_register_simd_operand
				       (operand->fmt) == 1)
				      && !strcmp (reg->name,
						  ext_oper->operand.name))
				    flg = 1;
				  break;
				case ARC_REGISTER_SIMD_I:
				  if ((ARC700_register_simd_operand
				       (operand->fmt) == 3)
				      && !strcmp (reg->name,
						  ext_oper->operand.name))
				    flg = 1;
				  break;
				case ARC_REGISTER_SIMD_K:
				  if ((ARC700_register_simd_operand
				       (operand->fmt) == 4)
				      && !strcmp (reg->name,
						  ext_oper->operand.name))
				    flg = 1;
				  break;
				case ARC_REGISTER_SIMD_DR:
				  if ((ARC700_register_simd_operand
				       (operand->fmt) == 2)
				      && !strcmp (reg->name,
						  ext_oper->operand.name))
				    flg = 1;
				  break;
				default:
				  break;
				}
			      if (flg){
				break;
				  }
			      ext_oper = ext_oper->next;
			    } /* end while(ext_oper ) */

			  if (!ext_oper)
			    break; /* Move on to next syntax.  */
			}

		      /*Ashwin: Checking if SIMD registers dont try to sub
			any of the Core registers. */
		      if (!ARC700_register_simd_operand (operand->fmt))
			{
			  if ((reg->flags & ARC_REGISTER_SIMD_VR )
			      || (reg->flags & ARC_REGISTER_SIMD_I )
			      || (reg->flags & ARC_REGISTER_SIMD_K )
			      || (reg->flags & ARC_REGISTER_SIMD_DR)
			     )
			    break;
			}


		      /* For conditional code instruction (ex: addeq) and
			 some 16-bit insns, the destination register should
			 be same as that of first source register. Ensure
			 that same register is matched for first and second
			 occurance of the operand's format 'B'(or 'b') in the
			 instruction's syntax being matched */
		      /* Added # as destination version of B */
		      if ((*syn == 'B') || (*syn == 'b') || (*syn == '#') || (*syn == ';'))
			{
			  if (regb_p && regb != reg)
			    {
			      break;
			    }
			  else
			    {
			      regb_p = 1;
			      regb = reg;
			    }
			}
		      /*ARCv2 Specific: h reg must be the same if appears multiple times*/
		      if (*syn == 128)
			{
			  if (regh != NULL && regh != reg)
			    {
			      break;
			    }
			  else
			    {
			      regh = reg;
			    }
			}

		      /* Try next insn syntax, if input operand is a auxiliary
			 regiser but the current operand being matched is
			 not a auxiliary register */
		      if ((arc_operands[reg->type].fmt == 'G')
			  && !(mods & ARC_MOD_AUXREG))
			break;
		    }
		}
	      /* Check if we expect a destination register.  */
	      else if (*syn == 'a' || *syn == 'A')
		as_bad ("symbol as destination register");
	      else
		{
		  if (strchr (str, '@'))
		    {
		      bfd_boolean needGOTSymbol = FALSE;
		      bfd_boolean try_addend = FALSE;
		      bfd_boolean force_ld_limm = TRUE;

		      if (!strncmp (str, "@gotpc", 6))
			{
			  str += 6;
			  if (assert_arc_pic_support ())
			    current_special_sym_flag = GOT_TYPE;
			  needGOTSymbol = TRUE;
			}
		      else if (!strncmp (str, "@plt", 4))
			{
			  str += 4;
			  if (assert_arc_pic_support ())
			    current_special_sym_flag = PLT_TYPE;
			  needGOTSymbol = TRUE;
			}
		      else if (!strncmp (str, "@gotoff", 7))
			{
			  str += 7;
			  if (assert_arc_pic_support ())
			    current_special_sym_flag = GOTOFF_TYPE;
			  try_addend = TRUE;
			  needGOTSymbol = TRUE;
			}
		      else if (!strncmp (str, "@pcl", 4))
			{
			  str += 4;
			  current_special_sym_flag = PCL_TYPE;
			  try_addend = TRUE;
			}
		      else if (!strncmp (str, "@sda", 4))
			{
			  if (!(mods & ARC_MOD_SDASYM))
			    {
			      //  fprintf (stderr, "Error: failed to match\n");
			      break;
			    }

			  str += 4;
			  current_special_sym_flag = SDA_REF_TYPE;
			  try_addend = TRUE;
			  needGOTSymbol = TRUE;
			  force_ld_limm = FALSE;
			}
		      else if (!strncmp (str, "@tlsgd", 6))
			{
			  str += 6;
			  current_special_sym_flag = TLSGD_TYPE;
			  needGOTSymbol = TRUE;
			}
		      else if (!strncmp (str, "@tlsie", 6))
			{
			  str += 6;
			  current_special_sym_flag = TLSIE_TYPE;
			  needGOTSymbol = TRUE;
			}
		      else if (!strncmp (str, "@tpoff", 6))
			{
			  str += 6;
			  current_special_sym_flag = TPOFF_TYPE;
			  if (*str == '9')
			    {
			      str++;
			      current_special_sym_flag = TPOFF9_TYPE;
			      force_ld_limm = FALSE;
			    }
			  try_addend = TRUE;
			}
		      else if (!strncmp (str, "@dtpoff", 7))
			{
			  str += 7;
			  current_special_sym_flag = DTPOFF_TYPE;
			  if (*str == '9')
			    {
			      str++;
			      current_special_sym_flag = DTPOFF9_TYPE;
			      force_ld_limm = FALSE;
			    }
			  gas_assert (exp.X_op == O_symbol);
			  gas_assert (exp.X_op_symbol == NULL);
			  if (*str == '@')
			    {
			      char *orig_line = input_line_pointer;
			      char c;
			      symbolS *base;
			      input_line_pointer = ++str;
			      c = get_symbol_end ();
			      base = symbol_find_or_make (str);
			      exp.X_op = O_subtract;
			      exp.X_op_symbol = base;
			      str = input_line_pointer;
			      *str = c;
			      input_line_pointer = orig_line;
			    }
			  try_addend = TRUE;
			}
		      else
			force_ld_limm = FALSE;

		      if (try_addend)
			{
			  /* Now check for identifier@XXX+constant */
			  if (*(str) == '-' || *(str) == '+')
			    {
			      char *orig_line = input_line_pointer;
			      expressionS new_exp;

			      input_line_pointer = str;
			      expression (&new_exp);
			      if (new_exp.X_op == O_constant)
				{
				  exp.X_add_number += new_exp.X_add_number;
				  str = input_line_pointer;
				}
			      input_line_pointer = orig_line;
			    }
			}

		      /* Force GOT symbols to be limm in case of pcl-relative
			 and large offset tpoff ld instructions.
			 If we had link-time relaxation, we'd want to defer
			 the decision till then - at least for tpoff.  */
		      if (arc_cond_p ==0
			  && force_ld_limm
			  && (insn_name[0] == 'l' || insn_name[0] == 'L')
			  && (insn_name[1] == 'd' || insn_name[1] == 'D')
			  && (!(insn_name[2] == '_')) )
			break;

		      /* In any of the above PIC related cases we would
			 have to make a GOT symbol if it is NULL.  */
		      if (needGOTSymbol && GOT_symbol == NULL)
			GOT_symbol
			  = symbol_find_or_make (GLOBAL_OFFSET_TABLE_NAME);
		    } /* End of '@' processing.  */
		  else if (mods & ARC_MOD_SDASYM)
		    {
		      //			  fprintf (stderr, "Not the sda syntax string. Trying next ********\n");
		      break;
		    }

		  /* Check the st/ld mnemonic:It should be able to
		     accomodate an immediate. Hence, no register
		     here!*/
		  if (!ac_constant_operand(operand)
		      && ( ((insn_name[0] == 'l' || insn_name[0] == 'L')
			    && (insn_name[1] == 'd' || insn_name[1] == 'D')) ||
			   ((insn_name[0] == 's' || insn_name[0] == 'S')
			    && (insn_name[1] == 't' || insn_name[1] == 'T'))))
		    {
		      /* It is a register of some sort. We cannot
			 do fixups on registers.*/
		      break;
		    }

		  /* We need to generate a fixup for this expression.  */
		  if (fc >= MAX_FIXUPS)
		    as_fatal ("too many fixups");
		  fixups[fc].exp = exp;
		  fixups[fc].modifier_flags = mods;

		  /* We don't support shimm relocs. break here to force
		     the assembler to output a limm.  */
/*
		 #define IS_REG_SHIMM_OFFSET(o) ((o) == 'd')
		 if (IS_REG_SHIMM_OFFSET (*syn))
		 break;
*/
		  /* If this is a register constant (IE: one whose
		     register value gets stored as 61-63) then this
		     must be a limm.  */
		  /* ??? This bit could use some cleaning up.
		     Referencing the format chars like this goes
		     against style.  */
		  if (IS_SYMBOL_OPERAND (*syn))
		    {
		      const char *junk;
		      limm_reloc_p = 1;
		      /* Save this, we don't yet know what reloc to use.  */
		      fix_up_at = fc;
		      /* Tell insert_reg we need a limm.  This is
			 needed because the value at this point is
			 zero, a shimm.  */
		      /* ??? We need a cleaner interface than this.  */
		      (*arc_operands[arc_operand_map['Q']].insert)
			(insn, &insn2,operand, mods, reg, 0L, &junk);
		      fixups[fc].opindex = arc_operand_map[(int) *syn]; //arc_operand_map[0];
		    }
		  else if (*syn == 'd')
		    {
		      /*ARCv2: This is a bbit or br. I need to
			override this FIXUP to handle also the Ybit*/
		      fix_up4_bbitbr = fc;
		      fixups[fc].opindex = arc_operand_map[(int) *syn];
		    }
		  else
		    fixups[fc].opindex = arc_operand_map[(int) *syn];
		  ++fc;
		  value = 0;
		}

	      /* The sda modifier is allowed only with symbols */
	      if ((mods & ARC_MOD_SDASYM) && exp.X_op != O_symbol
		  /* ??? But it appears also for ordinary load?  */
		  && current_special_sym_flag != DTPOFF9_TYPE)
		break;

	      /* Insert the register or expression into the instruction.  */
	      if (operand->insert)
		{
		  const char *errmsg = NULL;
		  insn = (*operand->insert) (insn,&insn2, operand, mods,
					     reg, (long) value, &errmsg);
		  if (errmsg != (const char *) NULL)
		    {
		      last_errmsg = errmsg;
		      if (operand->flags & ARC_OPERAND_ERROR)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_bad (errmsg, "");
			  assembling_instruction = 0;
			  return;
			}
		      else if (operand->flags & ARC_OPERAND_WARN)
			{
			  /* Yuk! This is meant to have a literal argument, so
			     it can be translated. We add a dummy empty string
			     argument, which keeps -Wformat-security happy for
			     now. */
			  as_warn (errmsg, "");
			}
		      break;
		    }
		}
	      else
		{
		  switch (operand->fmt)
		    {
		    case 'K':
		      insn |= ((value & 0x3f) << operand->shift);
		      insn |= ((value >>6 ) & 0x3f);
		      break;
		    case 'l':
		      insn |= (value >> 2) << operand->shift;
		      break;
		    case 'E':
		      insn |= value << operand->shift;
		      break;
		    default:
		      insn |= ((value & ((1 << operand->bits) - 1))
			       << operand->shift);
		    }
		}

	      insn |= convert_scaled;
	      ++syn;
	    }
	} /* end for(str=start,...) */
      /* If we're at the end of the syntax string, we're done.  */
      /* FIXME: try to move this to a separate function.  */
      if (*syn == '\0')
	{
	  int i;
	  char *f;
	  long limm, limm_p;
	  const char *errmsg=0;
	  const struct arc_operand *operand;
#if DEBUG_INST_PATTERN
fprintf (stdout, "Matched syntax %s\n", opcode->syntax);
#endif
	  if(!lm_present && !(opcode->flags & AC_SIMD_SETLM))
	      insn2 |= (0xff << 15);
	  if(opcode->flags & ARC_SIMD_ZERVA)
	    {
	      /* No  need for shadow declarations, can reuse. */
	      /* long limm_p, limm; */
	      limm_p = arc_opcode_limm_p (&limm);
	      if(limm_p)
		  insn2 = insn2+(limm&0x7fff);
	      operand = &arc_operands[arc_operand_map[zer_rega.type]];
	      if(operand->insert){
		  insn = (*operand->insert) (insn,&insn2, operand, mods,
					     &zer_rega, (long)zer_rega.value, &errmsg);
		  }
	      else
		  {
		  insn |= ((zer_rega.value & ((1 << operand->bits) - 1))
			   <<  operand->shift);
		  }

	      }
	  if(opcode->flags & ARC_SIMD_ZERVB)
	      {
	      /* No  need for shadow declarations, can reuse. */
	      /* long limm_p, limm; */
	      limm_p = arc_opcode_limm_p (&limm);
	      if(limm_p)
		  insn2 = insn2+(limm&0x7fff);
	      operand = &arc_operands[arc_operand_map[zer_regb.type]];
	      insn2 = insn2+(limm&0x7fff);
	      if(operand->insert)
		  {
		  insn = (*operand->insert) (insn,&insn2, operand, mods,
					     &zer_regb, (long)zer_regb.value, &errmsg);
		  }
	      else
		  {
		  insn |= ((zer_regb.value & ((1 << operand->bits) - 1)) <<
		      operand->shift);
		  }


	      }
	  if(opcode->flags & ARC_SIMD_ZERVC)
	      {
	      /* No  need for shadow declarations, can reuse. */
	      /* long limm_p, limm; */
	      limm_p = arc_opcode_limm_p (&limm);
	      if(limm_p)
		  insn2 = insn2+(limm&0x7fff);
	      operand = &arc_operands[arc_operand_map[zer_regc.type]];
	      if(operand->insert){
		  insn = (*operand->insert) (insn,&insn2, operand, mods,
					     &zer_regc, (long)zer_regc.value, &errmsg);
		  }
	      else
		  {
		  insn |= ((zer_regc.value & ((1 << operand->bits) - 1)) <<
		      operand->shift);
		  }
	      }
	  if(opcode->flags&ARC_SIMD_SETLM){
	      insn2 |= (0x3f)<<23;
	      }
	  /* For the moment we assume a valid `str' can only contain blanks
	     now.  IE: We needn't try again with a longer version of the
	     insn and it is assumed that longer versions of insns appear
	     before shorter ones (eg: lsr r2,r3,1 vs lsr r2,r3).  */

	  while (ISSPACE (*str))
	    ++str;

	  if (!is_end_of_line[(unsigned char) *str])
	    as_bad ("junk at end of line: `%s'", str);

	  /* Is there a limm value?  */
	  limm_p = arc_opcode_limm_p (&limm);
	  if(insn>>27==0x0a){
	      limm_p = 1;
	      limm = insn2;
	      }

	  /* Perform various error and warning tests.  */

	  {
	    static int in_delay_slot_p = 0;
	    static int prev_insn_needs_cc_nop_p = 0;
	    /* delay slot type seen */
	    int delay_slot_type = ARC_DELAY_NONE;
	    /* conditional execution flag seen */
	    int conditional = 0;
	    /* 1 if condition codes are being set */
	    int cc_set_p = 0;
	    /* 1 if conditional branch, including `b' "branch always" */
	    int cond_branch_p = opcode->flags & ARC_OPCODE_COND_BRANCH;

	    for (i = 0; i < num_suffixes; ++i)
	      {
		switch (arc_operands[insn_suffixes[i]->type].fmt)
		  {
		  case 'n':
		  case 'N':
		    delay_slot_type = insn_suffixes[i]->value;
		    break;
		  case 'q':
		    conditional = insn_suffixes[i]->value;
		    if ((arc_mach_type != bfd_mach_arc_arc700 ||
			 arc_mach_type != bfd_mach_arc_arcv2)
		       && conditional > 15
		       && !ext_suffix_p)
		      {
			/* It is invalid for the ARC 600 and
			   A5 to have condition codes ss and sc
			 */
			as_bad ("Invalid condition code \n");
		      }
		    break;
		  case 'f':
		    cc_set_p = 1;
		    break;
		  }
	      }
	    /* Some short instructions are not defined having the %N attribute*/
	    if (em_jumplink_or_jump_insn (insn, compact_insn_16))
	      {
		delay_slot_type = ARC_DELAY_JUMP;
	      }

	    insert_last_insn (insn, delay_slot_type, limm_p,
			      fixups[0].exp.X_add_symbol);

	    /* Putting an insn with a limm value in a delay slot is supposed to
	       be legal, but let's warn the user anyway.  Ditto for 8 byte
	       jumps with delay slots.  */
	    if (in_delay_slot_p && limm_p)
	      as_bad ("Instruction with long immediate data in delay slot");

	    if (delay_slot_type != ARC_DELAY_NONE
	      && limm_p && arc_insn_not_jl (insn)) /* except for jl  addr */
	      as_bad ("8 byte jump instruction with delay slot");

	    if (in_delay_slot_p)
	      {
		if (ac_branch_or_jump_insn (insn, compact_insn_16)
		    || em_branch_or_jump_insn (insn, compact_insn_16))
		  as_bad (_("branch/jump instruction in delay slot"));
		else if (ac_lpcc_insn (insn))
		  as_bad (_("lpcc instruction in delay slot"));
		else if (ARC700_rtie_insn (insn))
		  as_bad (_("rtie instruction in delay slot"));

		if (arc_mach_type != bfd_mach_arc_arc700 ||
		    arc_mach_type != bfd_mach_arc_arcv2)
		  {
		    if (a4_brk_insn (insn))
		      as_bad ("brk instruction in delay slot");
		    else if (ac_brk_s_insn (insn))
		      as_bad ("brk_s instruction in delay slot");
		  }
	      }

	    if (ac_lpcc_insn (insn))
	      {
		add_loop_target ((fixups[0].exp).X_add_symbol);
	      }

	    in_delay_slot_p = (delay_slot_type != ARC_DELAY_NONE) && !limm_p;

	    /* Warn when a conditional branch immediately follows a set of
	       the condition codes.  Note that this needn't be done if the
	       insn that sets the condition codes uses a limm.  */
	    if (cond_branch_p && conditional != 0 /* 0 = "always" */
		&& prev_insn_needs_cc_nop_p && arc_mach_type == bfd_mach_arc_a5)
	      as_warn ("conditional branch follows set of flags");
	    prev_insn_needs_cc_nop_p =
	      /* FIXME: ??? not required:
		 (delay_slot_type != ARC_DELAY_NONE) &&  */
	      cc_set_p && !limm_p;
	  }

	  /* Write out the instruction.
	     It is important to fetch enough space in one call to `frag_more'.
	     We use (f - frag_now->fr_literal) to compute where we are and we
	     don't want frag_now to change between calls.  */
	  if (limm_p)
	    {
	      if (compact_insn_16)
		{
		  f = frag_more (6);
		  md_number_to_chars (f, insn, 2);
		  md_number_to_chars (f + 2, limm, -4);
		  dwarf2_emit_insn (6);
		}
	      else
		{
		  f = frag_more (8);
		  md_number_to_chars (f, insn, -4);
		  md_number_to_chars (f + 4, limm, -4);
		  dwarf2_emit_insn (8);
		}
	    }
	  else if (limm_reloc_p)
	    /* We need a limm reloc, but the tables think we don't.  */
	    abort ();
	  else
	    {
	      if (compact_insn_16)
		{
		  f = frag_more (2);
		  md_number_to_chars (f, insn, 2);
		  dwarf2_emit_insn (2);
		}
	      else
		{
		  f = frag_more (4);
		  md_number_to_chars (f, insn, -4);
		  dwarf2_emit_insn (4);
		}
	    }

	  /* Create any fixups.  */
	  for (i = 0; i < fc; ++i)
	    {
	      int op_type;
	      bfd_reloc_code_real_type reloc_type;

	      int offset = 0; /* offset of the location within the frag where
				 the fixup occurs. */
	      int size = 4;   /* size of the fixup; mostly used for error
				 checking */
	      const struct arc_operand *operand2;

	      /* Create a fixup for this operand.
		 At this point we do not use a bfd_reloc_code_real_type for
		 operands residing in the insn, but instead just use the
		 operand index.  This lets us easily handle fixups for any
		 operand type, although that is admittedly not a very exciting
		 feature.  We pick a BFD reloc type in md_apply_fix.

		 Limm values (4 byte immediate "constants") must be treated
		 normally because they're not part of the actual insn word
		 and thus the insertion routines don't handle them.  */

	      if (arc_operands[fixups[i].opindex].flags & ARC_OPERAND_LIMM)
	      {
		  /* Modify the fixup addend as required by the cpu.  */
		  fixups[i].exp.X_add_number += arc_limm_fixup_adjust (insn);
		  op_type = fixups[i].opindex;
		  /* FIXME: can we add this data to the operand table?  */
		  if (op_type == arc_operand_map['L'])
		    {
		      reloc_type = BFD_RELOC_ARC_32_ME;
		      GAS_DEBUG_PIC (reloc_type);
		    }
		  else
		    abort ();
		  reloc_type = get_arc_exp_reloc_type (reloc_type,
						       BFD_RELOC_ARC_PC32,
						       &fixups[i].exp);
		  GAS_DEBUG_PIC (reloc_type);
	      }
	      else
		{
		  op_type = get_arc_exp_reloc_type (fixups[i].opindex,
						    BFD_RELOC_ARC_PC32,
						    &fixups[i].exp);
		  reloc_type = op_type + (int) BFD_RELOC_UNUSED;
		}
	      switch (current_special_sym_flag)
		{
		case SDA_REF_TYPE:
		  reloc_type = arc_get_sda_reloc (insn, compact_insn_16);
		  break;
		case GOT_TYPE:
		  reloc_type = BFD_RELOC_ARC_GOTPC32;
		  break;
		case PLT_TYPE:
		  reloc_type = BFD_RELOC_ARC_PLT32;
		  break;
		case GOTOFF_TYPE:
		  reloc_type = BFD_RELOC_ARC_GOTOFF;
		  break;
		case PCL_TYPE:
		  reloc_type = BFD_RELOC_ARC_PC32;
		  /* The hardware calculates relative to the start of the insn
		     (actually, pcl, but that part is sorted out later), but
		     this relocation is relative to the location of the LIMM.
		     When tc_gen_reloc translates BFD_RELOC_ARC_32_ME to
		     BFD_RELOC_ARC_PC32, to can use the full addresses, but
		     we can't use these here.  Setting the reloc type here
		     to BFD_RELOC_ARC_32_ME doesn't have the desired effect,
		     since tc_gen_reloc won't see fixups that have been
		     resolved locally.
		     Expressions with . have in principle the same problem,
		     but they don't force a LIMM - except for add.
		     FIXME: should probably remove the add special case and
		     make gcc use -. instead of @pcl in cases that don't need
		     a LIMM.  */
		  fixups[i].exp.X_add_number += 4;
		  break;
		case TLSGD_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_GD_GOT;
		  break;
		case TLSIE_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_IE_GOT;
		  break;
		case TPOFF9_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_LE_S9;
		  if (!arc_cond_p)
		    break;
		  /* Fall through.  */
		case TPOFF_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_LE_32;
		  break;
		case DTPOFF9_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_DTPOFF_S9;
		  if (!arc_cond_p)
		    break;
		  /* Fall through.  */
		case DTPOFF_TYPE:
		  reloc_type = BFD_RELOC_ARC_TLS_DTPOFF;
		  break;
		default:
		  break;
		}
	      operand2 = &arc_operands[op_type];

	      /* Calculate appropriate offset and size for the fixup */
	      if (compact_insn_16)
		{
		  /* If limm is needed */
		  if ((operand2->flags & ARC_OPERAND_LIMM)
		      && (!(fixups[i].modifier_flags & ARC_MOD_SDASYM) || ac_add_reg_sdasym_insn (insn) || reloc_type == BFD_RELOC_ARC_GOTPC32))
		    {
		      offset = 2;
		    }
		  else
		    {
		      size = 2;
		    }
		}
	      else /* for 32-bit instructions */
		{
		  /* If limm is needed */
		  if ((operand2->flags & ARC_OPERAND_LIMM)
		      && (!(fixups[i].modifier_flags & ARC_MOD_SDASYM) || ac_add_reg_sdasym_insn (insn) || reloc_type == BFD_RELOC_ARC_GOTPC32))
		    offset = 4;
		}

	      fix_new_exp (frag_now,
			   ((f - frag_now->fr_literal) + offset),
			   /* + (operand2->flags & ARC_OPERAND_LIMM ? 4 : 0)),*/
			   size,
			   &fixups[i].exp,
			   (current_special_sym_flag == PLT_TYPE)?0:
			   (operand2->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
			   || current_special_sym_flag == PCL_TYPE,
			   (bfd_reloc_code_real_type) reloc_type);
	    }
	  assembling_instruction = 0;
	  return;
	}

      /* Try the next entry.  */
    }

  if (NULL == last_errmsg){
    as_bad ("bad instruction `%s'", start);
      }
  else
    {
      /* Yuk! This is meant to have a literal argument, so
	 it can be translated. We add a dummy empty string
	 argument, which keeps -Wformat-security happy for
	 now. */
      as_bad (last_errmsg, "");
    }
  assembling_instruction = 0;
}

/* Frobbers.  */

#if 0
/* Set the real name if the .rename pseudo-op was used.
   Return 1 if the symbol should not be included in the symbol table.  */

int
arc_frob_symbol (sym)
     symbolS *sym;
{
  if (sym->sy_tc.real_name != (char *) NULL)
    S_SET_NAME (sym, sym->sy_tc.real_name);

  return 0;
}
#endif
/*
 * Here we decide which fixups can be adjusted to make them relative to
 * the beginning of the section instead of the symbol.  Basically we need
 * to make sure that the dynamic relocations are done correctly, so in
 * some cases we force the original symbol to be used.
 */
int
tc_arc_fix_adjustable (fixP)
     fixS * fixP;
{

  /* Prevent all adjustments to global symbols. */
  if (S_IS_EXTERNAL (fixP->fx_addsy))
    return 0;
  if (S_IS_WEAK (fixP->fx_addsy))
    return 0;

  /* adjust_reloc_syms doesn't know about the GOT */
  if (fixP->fx_r_type == BFD_RELOC_ARC_GOTPC32
      || fixP->fx_r_type == BFD_RELOC_ARC_PLT32)
    return 0;
  return 1;
}

/* This is a function to handle alignment and fill in the
   gaps created with nop/nop_s.
*/
void
arc_handle_align (fragS* fragP)
{
  if ((fragP)->fr_type == rs_align_code)
    {
      char *dest = (fragP)->fr_literal + (fragP)->fr_fix;
      valueT count = ((fragP)->fr_next->fr_address
		      - (fragP)->fr_address - (fragP)->fr_fix);

      (fragP)->fr_var = 2;

      if (count & 1)/* Padding in the gap till the next 2-byte boundary
		       with 0s.  */
	{
	  (fragP)->fr_fix++;
	  *dest++ = 0;
	}
      md_number_to_chars (dest, 0x78e0, 2);  /*writing nop_s */
    }
}

static void
arc_extra_reloc (int r_type)
{
  char *sym_name, c;
  symbolS *sym, *lab = NULL;

  if (*input_line_pointer == '@')
    input_line_pointer++;
  sym_name = input_line_pointer;
  c = get_symbol_end ();
  sym = symbol_find_or_make (sym_name);
  *input_line_pointer = c;
  if (c == ',' && r_type == BFD_RELOC_ARC_TLS_GD_LD)
    {
      char *lab_name = ++input_line_pointer;
      c = get_symbol_end ();
      lab = symbol_find_or_make (lab_name);
      *input_line_pointer = c;
    }
  fixS *fixP
    = fix_new (frag_now,	/* Which frag?  */
	       frag_now_fix (),	/* Where in that frag?  */
               2,		/* size: 1, 2, or 4 usually.  */
	       sym,		/* X_add_symbol.  */
	       0,		/* X_add_number.  */
	       FALSE,		/* TRUE if PC-relative relocation.  */
	       r_type		/* Relocation type.  */);
  fixP->fx_subsy = lab;
}
