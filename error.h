typedef enum {
    JE_OK = 0,		/* No error */
    JE_BAD_LVALUE,	/* Attempt to assign to a literal */
    JE_UNKNOWN_VAR,	/* Unknown variable */
    JE_NOT_OBJECT,	/* Attempt to access member in a non-object */
    JE_NOT_KEY,		/* Attempt to use a non-key as a member label */
    JE_UNKNOWN_MEMBER,	/* Attempt to access a member that isn't in object */
    JE_BAD_SUB_KEY,	/* Invalid key for [key:value] subscript */
    JE_UNKNOWN_SUB,	/* No element found with requested subscript */
    JE_BAD_SUB,		/* Bad subscript value */
    JE_CONST,		/* Can't change a const */
    JE_APPEND,		/* Can't append to a non-array */
} json_errorcode_t;
