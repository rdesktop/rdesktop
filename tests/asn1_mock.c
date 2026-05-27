#include <libtasn1.h>

int
asn1_array2tree(const asn1_static_node *array, asn1_node *definitions,
                char *errorDescription)
{
        (void) array;
        (void) definitions;
        (void) errorDescription;

        return ASN1_ELEMENT_NOT_FOUND;
}

int
asn1_create_element(asn1_node_const definitions, const char *source_name,
                    asn1_node *element)
{
        (void) definitions;
        (void) source_name;
        (void) element;

        return ASN1_ELEMENT_NOT_FOUND;
}

int
asn1_write_value(asn1_node node_root, const char *name, const void *ivalue,
                 int len)
{
        (void) node_root;
        (void) name;
        (void) ivalue;
        (void) len;

        return ASN1_ELEMENT_NOT_FOUND;
}

int
asn1_read_value(asn1_node_const root, const char *name, void *ivalue,
                int *len)
{
        (void) root;
        (void) name;
        (void) ivalue;
        (void) len;

        return ASN1_ELEMENT_NOT_FOUND;
}

int
asn1_der_coding(asn1_node_const element, const char *name, void *ider,
                int *len, char *ErrorDescription)
{
        (void) element;
        (void) name;
        (void) ider;
        (void) len;
        (void) ErrorDescription;

        return ASN1_ELEMENT_NOT_FOUND;
}

int
asn1_der_decoding(asn1_node *element, const void *ider, int ider_len,
                  char *errorDescription)
{
        (void) element;
        (void) ider;
        (void) ider_len;
        (void) errorDescription;

        return ASN1_ELEMENT_NOT_FOUND;
}

const char *
asn1_strerror(int error)
{
        (void) error;

        return "mock ASN.1 error";
}
