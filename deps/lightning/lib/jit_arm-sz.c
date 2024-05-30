
#if __WORDSIZE == 32
#if !defined(__ARM_PCS_VFP)
#define JIT_INSTR_MAX 144
    0,	/* data */
    0,	/* live */
    20,	/* align */
    0,	/* save */
    0,	/* load */
    4,	/* skip */
    2,	/* #name */
    0,	/* #note */
    0,	/* label */
    30,	/* prolog */
    0,	/* ellipsis */
    0,	/* va_push */
    0,	/* allocai */
    0,	/* allocar */
    0,	/* arg_c */
    0,	/* arg_s */
    0,	/* arg_i */
    0,	/* arg_l */
    0,	/* getarg_c */
    0,	/* getarg_uc */
    0,	/* getarg_s */
    0,	/* getarg_us */
    0,	/* getarg_i */
    0,	/* getarg_ui */
    0,	/* getarg_l */
    0,	/* putargr_c */
    0,	/* putargi_c */
    0,	/* putargr_uc */
    0,	/* putargi_uc */
    0,	/* putargr_s */
    0,	/* putargi_s */
    0,	/* putargr_us */
    0,	/* putargi_us */
    0,	/* putargr_i */
    0,	/* putargi_i */
    0,	/* putargr_ui */
    0,	/* putargi_ui */
    0,	/* putargr_l */
    0,	/* putargi_l */
    4,	/* va_start */
    8,	/* va_arg */
    28,	/* va_arg_d */
    0,	/* va_end */
    4,	/* addr */
    12,	/* addi */
    4,	/* addcr */
    8,	/* addci */
    4,	/* addxr */
    4,	/* addxi */
    4,	/* subr */
    12,	/* subi */
    4,	/* subcr */
    8,	/* subci */
    4,	/* subxr */
    4,	/* subxi */
    16,	/* rsbi */
    8,	/* mulr */
    12,	/* muli */
    4,	/* qmulr */
    12,	/* qmuli */
    4,	/* qmulr_u */
    8,	/* qmuli_u */
    32,	/* divr */
    36,	/* divi */
    24,	/* divr_u */
    28,	/* divi_u */
    18,	/* qdivr */
    22,	/* qdivi */
    18,	/* qdivr_u */
    22,	/* qdivi_u */
    24,	/* remr */
    32,	/* remi */
    24,	/* remr_u */
    28,	/* remi_u */
    4,	/* andr */
    12,	/* andi */
    4,	/* orr */
    12,	/* ori */
    4,	/* xorr */
    12,	/* xori */
    4,	/* lshr */
    4,	/* lshi */
    4,	/* rshr */
    4,	/* rshi */
    4,	/* rshr_u */
    4,	/* rshi_u */
    4,	/* negr */
    4,	/* negi */
    4,	/* comr */
    4,	/* comi */
    14,	/* ltr */
    14,	/* lti */
    14,	/* ltr_u */
    14,	/* lti_u */
    14,	/* ler */
    14,	/* lei */
    14,	/* ler_u */
    14,	/* lei_u */
    14,	/* eqr */
    14,	/* eqi */
    14,	/* ger */
    14,	/* gei */
    14,	/* ger_u */
    14,	/* gei_u */
    14,	/* gtr */
    14,	/* gti */
    14,	/* gtr_u */
    14,	/* gti_u */
    14,	/* ner */
    14,	/* nei */
    4,	/* movr */
    8,	/* movi */
    8,	/* movnr */
    8,	/* movzr */
    42,	/* casr */
    46,	/* casi */
    8,	/* extr_c */
    4,	/* exti_c */
    4,	/* extr_uc */
    4,	/* exti_uc */
    8,	/* extr_s */
    4,	/* exti_s */
    8,	/* extr_us */
    4,	/* exti_us */
    0,	/* extr_i */
    0,	/* exti_i */
    0,	/* extr_ui */
    0,	/* exti_ui */
    20,	/* bswapr_us */
    4,	/* bswapi_us */
    16,	/* bswapr_ui */
    8,	/* bswapi_ui */
    0,	/* bswapr_ul */
    0,	/* bswapi_ul */
    20,	/* htonr_us */
    4,	/* htoni_us */
    16,	/* htonr_ui */
    8,	/* htoni_ui */
    0,	/* htonr_ul */
    0,	/* htoni_ul */
    4,	/* ldr_c */
    12,	/* ldi_c */
    4,	/* ldr_uc */
    12,	/* ldi_uc */
    4,	/* ldr_s */
    12,	/* ldi_s */
    4,	/* ldr_us */
    12,	/* ldi_us */
    4,	/* ldr_i */
    12,	/* ldi_i */
    0,	/* ldr_ui */
    0,	/* ldi_ui */
    0,	/* ldr_l */
    0,	/* ldi_l */
    4,	/* ldxr_c */
    12,	/* ldxi_c */
    4,	/* ldxr_uc */
    12,	/* ldxi_uc */
    4,	/* ldxr_s */
    12,	/* ldxi_s */
    4,	/* ldxr_us */
    12,	/* ldxi_us */
    4,	/* ldxr_i */
    12,	/* ldxi_i */
    0,	/* ldxr_ui */
    0,	/* ldxi_ui */
    0,	/* ldxr_l */
    0,	/* ldxi_l */
    4,	/* str_c */
    12,	/* sti_c */
    4,	/* str_s */
    12,	/* sti_s */
    4,	/* str_i */
    12,	/* sti_i */
    0,	/* str_l */
    0,	/* sti_l */
    4,	/* stxr_c */
    12,	/* stxi_c */
    4,	/* stxr_s */
    12,	/* stxi_s */
    4,	/* stxr_i */
    12,	/* stxi_i */
    0,	/* stxr_l */
    0,	/* stxi_l */
    8,	/* bltr */
    8,	/* blti */
    8,	/* bltr_u */
    8,	/* blti_u */
    8,	/* bler */
    8,	/* blei */
    8,	/* bler_u */
    8,	/* blei_u */
    8,	/* beqr */
    16,	/* beqi */
    8,	/* bger */
    8,	/* bgei */
    8,	/* bger_u */
    8,	/* bgei_u */
    8,	/* bgtr */
    8,	/* bgti */
    8,	/* bgtr_u */
    8,	/* bgti_u */
    8,	/* bner */
    16,	/* bnei */
    8,	/* bmsr */
    8,	/* bmsi */
    8,	/* bmcr */
    8,	/* bmci */
    8,	/* boaddr */
    8,	/* boaddi */
    8,	/* boaddr_u */
    8,	/* boaddi_u */
    8,	/* bxaddr */
    8,	/* bxaddi */
    8,	/* bxaddr_u */
    8,	/* bxaddi_u */
    8,	/* bosubr */
    8,	/* bosubi */
    8,	/* bosubr_u */
    8,	/* bosubi_u */
    8,	/* bxsubr */
    8,	/* bxsubi */
    8,	/* bxsubr_u */
    8,	/* bxsubi_u */
    12,	/* jmpr */
    72,	/* jmpi */
    4,	/* callr */
    20,	/* calli */
    0,	/* prepare */
    0,	/* pushargr_c */
    0,	/* pushargi_c */
    0,	/* pushargr_uc */
    0,	/* pushargi_uc */
    0,	/* pushargr_s */
    0,	/* pushargi_s */
    0,	/* pushargr_us */
    0,	/* pushargi_us */
    0,	/* pushargr_i */
    0,	/* pushargi_i */
    0,	/* pushargr_ui */
    0,	/* pushargi_ui */
    0,	/* pushargr_l */
    0,	/* pushargi_l */
    0,	/* finishr */
    0,	/* finishi */
    0,	/* ret */
    0,	/* retr_c */
    0,	/* reti_c */
    0,	/* retr_uc */
    0,	/* reti_uc */
    0,	/* retr_s */
    0,	/* reti_s */
    0,	/* retr_us */
    0,	/* reti_us */
    0,	/* retr_i */
    0,	/* reti_i */
    0,	/* retr_ui */
    0,	/* reti_ui */
    0,	/* retr_l */
    0,	/* reti_l */
    0,	/* retval_c */
    0,	/* retval_uc */
    0,	/* retval_s */
    0,	/* retval_us */
    0,	/* retval_i */
    0,	/* retval_ui */
    0,	/* retval_l */
    276,	/* epilog */
    0,	/* arg_f */
    0,	/* getarg_f */
    0,	/* putargr_f */
    0,	/* putargi_f */
    24,	/* addr_f */
    24,	/* addi_f */
    24,	/* subr_f */
    24,	/* subi_f */
    24,	/* rsbi_f */
    24,	/* mulr_f */
    24,	/* muli_f */
    24,	/* divr_f */
    24,	/* divi_f */
    12,	/* negr_f */
    0,	/* negi_f */
    12,	/* absr_f */
    0,	/* absi_f */
    20,	/* sqrtr_f */
    0,	/* sqrti_f */
    24,	/* ltr_f */
    30,	/* lti_f */
    24,	/* ler_f */
    32,	/* lei_f */
    24,	/* eqr_f */
    30,	/* eqi_f */
    24,	/* ger_f */
    30,	/* gei_f */
    24,	/* gtr_f */
    30,	/* gti_f */
    28,	/* ner_f */
    32,	/* nei_f */
    56,	/* unltr_f */
    64,	/* unlti_f */
    56,	/* unler_f */
    64,	/* unlei_f */
    56,	/* uneqr_f */
    64,	/* uneqi_f */
    56,	/* unger_f */
    64,	/* ungei_f */
    56,	/* ungtr_f */
    64,	/* ungti_f */
    60,	/* ltgtr_f */
    68,	/* ltgti_f */
    28,	/* ordr_f */
    32,	/* ordi_f */
    56,	/* unordr_f */
    64,	/* unordi_f */
    20,	/* truncr_f_i */
    0,	/* truncr_f_l */
    28,	/* extr_f */
    22,	/* extr_d_f */
    8,	/* movr_f */
    16,	/* movi_f */
    8,	/* ldr_f */
    16,	/* ldi_f */
    8,	/* ldxr_f */
    16,	/* ldxi_f */
    8,	/* str_f */
    16,	/* sti_f */
    8,	/* stxr_f */
    16,	/* stxi_f */
    28,	/* bltr_f */
    32,	/* blti_f */
    28,	/* bler_f */
    32,	/* blei_f */
    28,	/* beqr_f */
    48,	/* beqi_f */
    28,	/* bger_f */
    32,	/* bgei_f */
    28,	/* bgtr_f */
    32,	/* bgti_f */
    28,	/* bner_f */
    32,	/* bnei_f */
    28,	/* bunltr_f */
    32,	/* bunlti_f */
    28,	/* bunler_f */
    32,	/* bunlei_f */
    60,	/* buneqr_f */
    68,	/* buneqi_f */
    28,	/* bunger_f */
    32,	/* bungei_f */
    28,	/* bungtr_f */
    32,	/* bungti_f */
    60,	/* bltgtr_f */
    68,	/* bltgti_f */
    28,	/* bordr_f */
    32,	/* bordi_f */
    28,	/* bunordr_f */
    32,	/* bunordi_f */
    0,	/* pushargr_f */
    0,	/* pushargi_f */
    0,	/* retr_f */
    0,	/* reti_f */
    0,	/* retval_f */
    0,	/* arg_d */
    0,	/* getarg_d */
    0,	/* putargr_d */
    0,	/* putargi_d */
    34,	/* addr_d */
    36,	/* addi_d */
    34,	/* subr_d */
    36,	/* subi_d */
    36,	/* rsbi_d */
    34,	/* mulr_d */
    36,	/* muli_d */
    34,	/* divr_d */
    36,	/* divi_d */
    20,	/* negr_d */
    0,	/* negi_d */
    20,	/* absr_d */
    0,	/* absi_d */
    26,	/* sqrtr_d */
    0,	/* sqrti_d */
    28,	/* ltr_d */
    34,	/* lti_d */
    28,	/* ler_d */
    36,	/* lei_d */
    28,	/* eqr_d */
    34,	/* eqi_d */
    28,	/* ger_d */
    34,	/* gei_d */
    28,	/* gtr_d */
    34,	/* gti_d */
    32,	/* ner_d */
    36,	/* nei_d */
    66,	/* unltr_d */
    72,	/* unlti_d */
    66,	/* unler_d */
    72,	/* unlei_d */
    66,	/* uneqr_d */
    72,	/* uneqi_d */
    66,	/* unger_d */
    72,	/* ungei_d */
    66,	/* ungtr_d */
    72,	/* ungti_d */
    70,	/* ltgtr_d */
    76,	/* ltgti_d */
    32,	/* ordr_d */
    36,	/* ordi_d */
    66,	/* unordr_d */
    72,	/* unordi_d */
    20,	/* truncr_d_i */
    0,	/* truncr_d_l */
    36,	/* extr_d */
    22,	/* extr_f_d */
    16,	/* movr_d */
    32,	/* movi_d */
    16,	/* ldr_d */
    24,	/* ldi_d */
    20,	/* ldxr_d */
    28,	/* ldxi_d */
    16,	/* str_d */
    24,	/* sti_d */
    20,	/* stxr_d */
    28,	/* stxi_d */
    32,	/* bltr_d */
    36,	/* blti_d */
    32,	/* bler_d */
    36,	/* blei_d */
    32,	/* beqr_d */
    52,	/* beqi_d */
    32,	/* bger_d */
    36,	/* bgei_d */
    32,	/* bgtr_d */
    36,	/* bgti_d */
    32,	/* bner_d */
    36,	/* bnei_d */
    32,	/* bunltr_d */
    36,	/* bunlti_d */
    32,	/* bunler_d */
    36,	/* bunlei_d */
    68,	/* buneqr_d */
    76,	/* buneqi_d */
    32,	/* bunger_d */
    36,	/* bungei_d */
    32,	/* bungtr_d */
    36,	/* bungti_d */
    68,	/* bltgtr_d */
    76,	/* bltgti_d */
    32,	/* bordr_d */
    36,	/* bordi_d */
    32,	/* bunordr_d */
    36,	/* bunordi_d */
    0,	/* pushargr_d */
    0,	/* pushargi_d */
    0,	/* retr_d */
    0,	/* reti_d */
    0,	/* retval_d */
    4,	/* movr_w_f */
    8,	/* movi_w_f */
    8,	/* movr_ww_d */
    20,	/* movi_ww_d */
    0,	/* movr_w_d */
    0,	/* movi_w_d */
    4,	/* movr_f_w */
    8,	/* movi_f_w */
    8,	/* movr_d_ww */
    12,	/* movi_d_ww */
    0,	/* movr_d_w */
    0,	/* movi_d_w */
    8,	/* clor */
    4,	/* cloi */
    4,	/* clzr */
    4,	/* clzi */
    12,	/* ctor */
    4,	/* ctoi */
    8,	/* ctzr */
    4,	/* ctzi */
    4,	/* rbitr */
    8,	/* rbiti */
    40,	/* popcntr */
    4,	/* popcnti */
    12,	/* lrotr */
    4,	/* lroti */
    4,	/* rrotr */
    4,	/* rroti */
    8,	/* extr */
    4,	/* exti */
    12,	/* extr_u */
    4,	/* exti_u */
    24,	/* depr */
    20,	/* depi */
    50,	/* qlshr */
    8,	/* qlshi */
    50,	/* qlshr_u */
    8,	/* qlshi_u */
    50,	/* qrshr */
    8,	/* qrshi */
    50,	/* qrshr_u */
    8,	/* qrshi_u */
    72,	/* unldr */
    44,	/* unldi */
    72,	/* unldr_u */
    44,	/* unldi_u */
    68,	/* unstr */
    44,	/* unsti */
    144,	/* unldr_x */
    80,	/* unldi_x */
    148,	/* unstr_x */
    96,	/* unsti_x */
    48,	/* fmar_f */
    0,	/* fmai_f */
    48,	/* fmsr_f */
    0,	/* fmsi_f */
    68,	/* fmar_d */
    0,	/* fmai_d */
    68,	/* fmsr_d */
    0,	/* fmsi_d */
    60,	/* fnmar_f */
    0,	/* fnmai_f */
    60,	/* fnmsr_f */
    0,	/* fnmsi_f */
    88,	/* fnmar_d */
    0,	/* fnmai_d */
    88,	/* fnmsr_d */
    0,	/* fnmsi_d */
    4,	/* hmulr */
    12,	/* hmuli */
    4,	/* hmulr_u */
    8,	/* hmuli_u */
    8,	/* ldxbr_c */
    4,	/* ldxbi_c */
    8,	/* ldxar_c */
    8,	/* ldxai_c */
    8,	/* ldxbr_uc */
    4,	/* ldxbi_uc */
    8,	/* ldxar_uc */
    8,	/* ldxai_uc */
    8,	/* ldxbr_s */
    4,	/* ldxbi_s */
    8,	/* ldxar_s */
    8,	/* ldxai_s */
    8,	/* ldxbr_us */
    4,	/* ldxbi_us */
    8,	/* ldxar_us */
    8,	/* ldxai_us */
    8,	/* ldxbr_i */
    4,	/* ldxbi_i */
    8,	/* ldxar_i */
    8,	/* ldxai_i */
    0,	/* ldxbr_ui */
    0,	/* ldxbi_ui */
    0,	/* ldxar_ui */
    0,	/* ldxai_ui */
    0,	/* ldxbr_l */
    0,	/* ldxbi_l */
    0,	/* ldxar_l */
    0,	/* ldxai_l */
    12, /* ldxbr_f */
    12, /* ldxbi_f */
    12, /* ldxar_f */
    12, /* ldxai_f */
    20, /* ldxbr_d */
    20, /* ldxbi_d */
    20, /* ldxar_d */
    20, /* ldxai_d */
    8,	/* stxbr_c */
    4,	/* stxbi_c */
    8,	/* stxar_c */
    8,	/* stxai_c */
    8,	/* stxbr_s */
    4,	/* stxbi_s */
    8,	/* stxar_s */
    8,	/* stxai_s */
    8,	/* stxbr_i */
    4,	/* stxbi_i */
    8,	/* stxar_i */
    8,	/* stxai_i */
    0,	/* stxbr_l */
    0,	/* stxbi_l */
    0,	/* stxar_l */
    0,	/* stxai_l */
    12, /* stxbr_f */
    12, /* stxbi_f */
    12, /* stxar_f */
    12, /* stxai_f */
    20, /* stxbr_d */
    20, /* stxbi_d */
    20, /* stxar_d */
    20, /* stxai_d */
#endif /* __ARM_PCS_VFP */
#endif /* __WORDSIZE */

#if __WORDSIZE == 32
#if defined(__ARM_PCS_VFP)
#define JIT_INSTR_MAX 144
    0,	/* data */
    0,	/* live */
    20,	/* align */
    0,	/* save */
    0,	/* load */
    4,	/* skip */
    2,	/* #name */
    0,	/* #note */
    0,	/* label */
    26,	/* prolog */
    0,	/* ellipsis */
    0,	/* va_push */
    0,	/* allocai */
    0,	/* allocar */
    0,	/* arg_c */
    0,	/* arg_s */
    0,	/* arg_i */
    0,	/* arg_l */
    0,	/* getarg_c */
    0,	/* getarg_uc */
    0,	/* getarg_s */
    0,	/* getarg_us */
    0,	/* getarg_i */
    0,	/* getarg_ui */
    0,	/* getarg_l */
    0,	/* putargr_c */
    0,	/* putargi_c */
    0,	/* putargr_uc */
    0,	/* putargi_uc */
    0,	/* putargr_s */
    0,	/* putargi_s */
    0,	/* putargr_us */
    0,	/* putargi_us */
    0,	/* putargr_i */
    0,	/* putargi_i */
    0,	/* putargr_ui */
    0,	/* putargi_ui */
    0,	/* putargr_l */
    0,	/* putargi_l */
    4,	/* va_start */
    8,	/* va_arg */
    16,	/* va_arg_d */
    0,	/* va_end */
    4,	/* addr */
    12,	/* addi */
    4,	/* addcr */
    8,	/* addci */
    4,	/* addxr */
    4,	/* addxi */
    4,	/* subr */
    12,	/* subi */
    4,	/* subcr */
    8,	/* subci */
    4,	/* subxr */
    4,	/* subxi */
    16,	/* rsbi */
    4,	/* mulr */
    12,	/* muli */
    4,	/* qmulr */
    12,	/* qmuli */
    4,	/* qmulr_u */
    8,	/* qmuli_u */
    32,	/* divr */
    36,	/* divi */
    24,	/* divr_u */
    28,	/* divi_u */
    18,	/* qdivr */
    22,	/* qdivi */
    18,	/* qdivr_u */
    22,	/* qdivi_u */
    24,	/* remr */
    32,	/* remi */
    24,	/* remr_u */
    28,	/* remi_u */
    4,	/* andr */
    12,	/* andi */
    4,	/* orr */
    12,	/* ori */
    4,	/* xorr */
    12,	/* xori */
    4,	/* lshr */
    4,	/* lshi */
    4,	/* rshr */
    4,	/* rshi */
    4,	/* rshr_u */
    4,	/* rshi_u */
    4,	/* negr */
    4,	/* negi */
    4,	/* comr */
    4,	/* comi */
    14,	/* ltr */
    14,	/* lti */
    14,	/* ltr_u */
    14,	/* lti_u */
    14,	/* ler */
    14,	/* lei */
    14,	/* ler_u */
    14,	/* lei_u */
    14,	/* eqr */
    14,	/* eqi */
    14,	/* ger */
    14,	/* gei */
    14,	/* ger_u */
    14,	/* gei_u */
    14,	/* gtr */
    14,	/* gti */
    14,	/* gtr_u */
    14,	/* gti_u */
    14,	/* ner */
    14,	/* nei */
    4,	/* movr */
    8,	/* movi */
    8,	/* movnr */
    8,	/* movzr */
    42,	/* casr */
    50,	/* casi */
    4,	/* extr_c */
    4,	/* exti_c */
    4,	/* extr_uc */
    4,	/* exti_uc */
    4,	/* extr_s */
    4,	/* exti_s */
    4,	/* extr_us */
    4,	/* exti_us */
    0,	/* extr_i */
    0,	/* exti_i */
    0,	/* extr_ui */
    0,	/* exti_ui */
    8,	/* bswapr_us */
    4,	/* bswapi_us */
    4,	/* bswapr_ui */
    8,	/* bswapi_ui */
    0,	/* bswapr_ul */
    0,	/* bswapi_ul */
    8,	/* htonr_us */
    4,	/* htoni_us */
    4,	/* htonr_ui */
    8,	/* htoni_ui */
    0,	/* htonr_ul */
    0,	/* htoni_ul */
    4,	/* ldr_c */
    12,	/* ldi_c */
    4,	/* ldr_uc */
    12,	/* ldi_uc */
    4,	/* ldr_s */
    12,	/* ldi_s */
    4,	/* ldr_us */
    12,	/* ldi_us */
    4,	/* ldr_i */
    12,	/* ldi_i */
    0,	/* ldr_ui */
    0,	/* ldi_ui */
    0,	/* ldr_l */
    0,	/* ldi_l */
    4,	/* ldxr_c */
    12,	/* ldxi_c */
    4,	/* ldxr_uc */
    12,	/* ldxi_uc */
    4,	/* ldxr_s */
    12,	/* ldxi_s */
    4,	/* ldxr_us */
    12,	/* ldxi_us */
    4,	/* ldxr_i */
    12,	/* ldxi_i */
    0,	/* ldxr_ui */
    0,	/* ldxi_ui */
    0,	/* ldxr_l */
    0,	/* ldxi_l */
    4,	/* str_c */
    12,	/* sti_c */
    4,	/* str_s */
    12,	/* sti_s */
    4,	/* str_i */
    12,	/* sti_i */
    0,	/* str_l */
    0,	/* sti_l */
    4,	/* stxr_c */
    12,	/* stxi_c */
    4,	/* stxr_s */
    12,	/* stxi_s */
    4,	/* stxr_i */
    12,	/* stxi_i */
    0,	/* stxr_l */
    0,	/* stxi_l */
    8,	/* bltr */
    8,	/* blti */
    8,	/* bltr_u */
    8,	/* blti_u */
    8,	/* bler */
    8,	/* blei */
    8,	/* bler_u */
    8,	/* blei_u */
    8,	/* beqr */
    16,	/* beqi */
    8,	/* bger */
    8,	/* bgei */
    8,	/* bger_u */
    8,	/* bgei_u */
    8,	/* bgtr */
    8,	/* bgti */
    8,	/* bgtr_u */
    8,	/* bgti_u */
    8,	/* bner */
    16,	/* bnei */
    8,	/* bmsr */
    8,	/* bmsi */
    8,	/* bmcr */
    8,	/* bmci */
    8,	/* boaddr */
    8,	/* boaddi */
    8,	/* boaddr_u */
    8,	/* boaddi_u */
    8,	/* bxaddr */
    8,	/* bxaddi */
    8,	/* bxaddr_u */
    8,	/* bxaddi_u */
    8,	/* bosubr */
    8,	/* bosubi */
    8,	/* bosubr_u */
    8,	/* bosubi_u */
    8,	/* bxsubr */
    8,	/* bxsubi */
    8,	/* bxsubr_u */
    8,	/* bxsubi_u */
    4,	/* jmpr */
    8,	/* jmpi */
    4,	/* callr */
    20,	/* calli */
    0,	/* prepare */
    0,	/* pushargr_c */
    0,	/* pushargi_c */
    0,	/* pushargr_uc */
    0,	/* pushargi_uc */
    0,	/* pushargr_s */
    0,	/* pushargi_s */
    0,	/* pushargr_us */
    0,	/* pushargi_us */
    0,	/* pushargr_i */
    0,	/* pushargi_i */
    0,	/* pushargr_ui */
    0,	/* pushargi_ui */
    0,	/* pushargr_l */
    0,	/* pushargi_l */
    0,	/* finishr */
    0,	/* finishi */
    0,	/* ret */
    0,	/* retr_c */
    0,	/* reti_c */
    0,	/* retr_uc */
    0,	/* reti_uc */
    0,	/* retr_s */
    0,	/* reti_s */
    0,	/* retr_us */
    0,	/* reti_us */
    0,	/* retr_i */
    0,	/* reti_i */
    0,	/* retr_ui */
    0,	/* reti_ui */
    0,	/* retr_l */
    0,	/* reti_l */
    0,	/* retval_c */
    0,	/* retval_uc */
    0,	/* retval_s */
    0,	/* retval_us */
    0,	/* retval_i */
    0,	/* retval_ui */
    0,	/* retval_l */
    16,	/* epilog */
    0,	/* arg_f */
    0,	/* getarg_f */
    0,	/* putargr_f */
    0,	/* putargi_f */
    4,	/* addr_f */
    8,	/* addi_f */
    4,	/* subr_f */
    8,	/* subi_f */
    8,	/* rsbi_f */
    4,	/* mulr_f */
    8,	/* muli_f */
    4,	/* divr_f */
    8,	/* divi_f */
    4,	/* negr_f */
    0,	/* negi_f */
    4,	/* absr_f */
    0,	/* absi_f */
    4,	/* sqrtr_f */
    0,	/* sqrti_f */
    18,	/* ltr_f */
    30,	/* lti_f */
    20,	/* ler_f */
    32,	/* lei_f */
    18,	/* eqr_f */
    30,	/* eqi_f */
    18,	/* ger_f */
    30,	/* gei_f */
    18,	/* gtr_f */
    30,	/* gti_f */
    18,	/* ner_f */
    30,	/* nei_f */
    18,	/* unltr_f */
    30,	/* unlti_f */
    18,	/* unler_f */
    30,	/* unlei_f */
    24,	/* uneqr_f */
    36,	/* uneqi_f */
    18,	/* unger_f */
    30,	/* ungei_f */
    18,	/* ungtr_f */
    30,	/* ungti_f */
    24,	/* ltgtr_f */
    36,	/* ltgti_f */
    18,	/* ordr_f */
    30,	/* ordi_f */
    18,	/* unordr_f */
    30,	/* unordi_f */
    8,	/* truncr_f_i */
    0,	/* truncr_f_l */
    8,	/* extr_f */
    4,	/* extr_d_f */
    4,	/* movr_f */
    12,	/* movi_f */
    4,	/* ldr_f */
    12,	/* ldi_f */
    8,	/* ldxr_f */
    16,	/* ldxi_f */
    4,	/* str_f */
    12,	/* sti_f */
    8,	/* stxr_f */
    16,	/* stxi_f */
    12,	/* bltr_f */
    24,	/* blti_f */
    12,	/* bler_f */
    24,	/* blei_f */
    12,	/* beqr_f */
    24,	/* beqi_f */
    12,	/* bger_f */
    24,	/* bgei_f */
    12,	/* bgtr_f */
    24,	/* bgti_f */
    12,	/* bner_f */
    24,	/* bnei_f */
    16,	/* bunltr_f */
    28,	/* bunlti_f */
    16,	/* bunler_f */
    28,	/* bunlei_f */
    20,	/* buneqr_f */
    32,	/* buneqi_f */
    16,	/* bunger_f */
    28,	/* bungei_f */
    12,	/* bungtr_f */
    24,	/* bungti_f */
    20,	/* bltgtr_f */
    32,	/* bltgti_f */
    12,	/* bordr_f */
    24,	/* bordi_f */
    12,	/* bunordr_f */
    24,	/* bunordi_f */
    0,	/* pushargr_f */
    0,	/* pushargi_f */
    0,	/* retr_f */
    0,	/* reti_f */
    0,	/* retval_f */
    0,	/* arg_d */
    0,	/* getarg_d */
    0,	/* putargr_d */
    0,	/* putargi_d */
    4,	/* addr_d */
    20,	/* addi_d */
    4,	/* subr_d */
    20,	/* subi_d */
    20,	/* rsbi_d */
    4,	/* mulr_d */
    20,	/* muli_d */
    4,	/* divr_d */
    20,	/* divi_d */
    4,	/* negr_d */
    0,	/* negi_d */
    4,	/* absr_d */
    0,	/* absi_d */
    4,	/* sqrtr_d */
    0,	/* sqrti_d */
    18,	/* ltr_d */
    34,	/* lti_d */
    20,	/* ler_d */
    36,	/* lei_d */
    18,	/* eqr_d */
    34,	/* eqi_d */
    18,	/* ger_d */
    34,	/* gei_d */
    18,	/* gtr_d */
    34,	/* gti_d */
    18,	/* ner_d */
    34,	/* nei_d */
    18,	/* unltr_d */
    34,	/* unlti_d */
    18,	/* unler_d */
    34,	/* unlei_d */
    24,	/* uneqr_d */
    40,	/* uneqi_d */
    18,	/* unger_d */
    34,	/* ungei_d */
    18,	/* ungtr_d */
    34,	/* ungti_d */
    24,	/* ltgtr_d */
    40,	/* ltgti_d */
    18,	/* ordr_d */
    34,	/* ordi_d */
    18,	/* unordr_d */
    34,	/* unordi_d */
    8,	/* truncr_d_i */
    0,	/* truncr_d_l */
    8,	/* extr_d */
    4,	/* extr_f_d */
    4,	/* movr_d */
    32,	/* movi_d */
    4,	/* ldr_d */
    12,	/* ldi_d */
    8,	/* ldxr_d */
    16,	/* ldxi_d */
    4,	/* str_d */
    12,	/* sti_d */
    8,	/* stxr_d */
    16,	/* stxi_d */
    12,	/* bltr_d */
    28,	/* blti_d */
    12,	/* bler_d */
    28,	/* blei_d */
    12,	/* beqr_d */
    36,	/* beqi_d */
    12,	/* bger_d */
    28,	/* bgei_d */
    12,	/* bgtr_d */
    28,	/* bgti_d */
    12,	/* bner_d */
    28,	/* bnei_d */
    16,	/* bunltr_d */
    32,	/* bunlti_d */
    16,	/* bunler_d */
    32,	/* bunlei_d */
    20,	/* buneqr_d */
    36,	/* buneqi_d */
    16,	/* bunger_d */
    32,	/* bungei_d */
    12,	/* bungtr_d */
    28,	/* bungti_d */
    20,	/* bltgtr_d */
    36,	/* bltgti_d */
    12,	/* bordr_d */
    28,	/* bordi_d */
    12,	/* bunordr_d */
    28,	/* bunordi_d */
    0,	/* pushargr_d */
    0,	/* pushargi_d */
    0,	/* retr_d */
    0,	/* reti_d */
    0,	/* retval_d */
    4,	/* movr_w_f */
    8,	/* movi_w_f */
    4,	/* movr_ww_d */
    16,	/* movi_ww_d */
    0,	/* movr_w_d */
    0,	/* movi_w_d */
    4,	/* movr_f_w */
    4,	/* movi_f_w */
    4,	/* movr_d_ww */
    12,	/* movi_d_ww */
    0,	/* movr_d_w */
    0,	/* movi_d_w */
    8,	/* clor */
    4,	/* cloi */
    4,	/* clzr */
    4,	/* clzi */
    12,	/* ctor */
    4,	/* ctoi */
    8,	/* ctzr */
    4,	/* ctzi */
    4,	/* rbitr */
    8,	/* rbiti */
    40,	/* popcntr */
    4,	/* popcnti */
    12,	/* lrotr */
    4,	/* lroti */
    4,	/* rrotr */
    4,	/* rroti */
    4,	/* extr */
    4,	/* exti */
    4,	/* extr_u */
    4,	/* exti_u */
    4,	/* depr */
    8,	/* depi */
    50,	/* qlshr */
    8,	/* qlshi */
    50,	/* qlshr_u */
    8,	/* qlshi_u */
    50,	/* qrshr */
    8,	/* qrshi */
    50,	/* qrshr_u */
    8,	/* qrshi_u */
    72,	/* unldr */
    44,	/* unldi */
    72,	/* unldr_u */
    44,	/* unldi_u */
    68,	/* unstr */
    44,	/* unsti */
    140,	/* unldr_x */
    76,	/* unldi_x */
    144,	/* unstr_x */
    92,	/* unsti_x */
    8,	/* fmar_f */
    0,	/* fmai_f */
    8,	/* fmsr_f */
    0,	/* fmsi_f */
    8,	/* fmar_d */
    0,	/* fmai_d */
    8,	/* fmsr_d */
    0,	/* fmsi_d */
    12,	/* fnmar_f */
    0,	/* fnmai_f */
    12,	/* fnmsr_f */
    0,	/* fnmsi_f */
    12,	/* fnmar_d */
    0,	/* fnmai_d */
    12,	/* fnmsr_d */
    0,	/* fnmsi_d */
    4,	/* hmulr */
    12,	/* hmuli */
    4,	/* hmulr_u */
    8,	/* hmuli_u */
    8,	/* ldxbr_c */
    4,	/* ldxbi_c */
    8,	/* ldxar_c */
    8,	/* ldxai_c */
    8,	/* ldxbr_uc */
    4,	/* ldxbi_uc */
    8,	/* ldxar_uc */
    8,	/* ldxai_uc */
    8,	/* ldxbr_s */
    4,	/* ldxbi_s */
    8,	/* ldxar_s */
    8,	/* ldxai_s */
    8,	/* ldxbr_us */
    4,	/* ldxbi_us */
    8,	/* ldxar_us */
    8,	/* ldxai_us */
    8,	/* ldxbr_i */
    4,	/* ldxbi_i */
    8,	/* ldxar_i */
    8,	/* ldxai_i */
    0,	/* ldxbr_ui */
    0,	/* ldxbi_ui */
    0,	/* ldxar_ui */
    0,	/* ldxai_ui */
    0,	/* ldxbr_l */
    0,	/* ldxbi_l */
    0,	/* ldxar_l */
    0,	/* ldxai_l */
    8,	/* ldxbr_f */
    8,	/* ldxbi_f */
    8,	/* ldxar_f */
    8,	/* ldxai_f */
    8,	/* ldxbr_d */
    8,	/* ldxbi_d */
    8,	/* ldxar_d */
    8,	/* ldxai_d */
    8,	/* stxbr_c */
    4,	/* stxbi_c */
    8,	/* stxar_c */
    8,	/* stxai_c */
    8,	/* stxbr_s */
    4,	/* stxbi_s */
    8,	/* stxar_s */
    8,	/* stxai_s */
    8,	/* stxbr_i */
    4,	/* stxbi_i */
    8,	/* stxar_i */
    8,	/* stxai_i */
    0,	/* stxbr_l */
    0,	/* stxbi_l */
    0,	/* stxar_l */
    0,	/* stxai_l */
    8,	/* stxbr_f */
    8,	/* stxbi_f */
    8,	/* stxar_f */
    8,	/* stxai_f */
    8,	/* stxbr_d */
    8,	/* stxbi_d */
    8,	/* stxar_d */
    8,	/* stxai_d */
#endif /* __ARM_PCS_VFP */
#endif /* __WORDSIZE */