#define SUCCEED 1
#define GFAILED 0
#define UNSURE -1

int test_srp( gnutls_session state);
int test_export( gnutls_session state);
int test_export_info( gnutls_session state);
int test_hello_extension( gnutls_session state);
int test_dhe( gnutls_session state);
int test_dhe_group( gnutls_session state);
int test_ssl3( gnutls_session state);
int test_aes( gnutls_session state);
int test_md5( gnutls_session state);
int test_sha( gnutls_session state);
int test_rmd( gnutls_session state);
int test_3des( gnutls_session state);
int test_arcfour( gnutls_session state);
int test_arcfour_40( gnutls_session state);
int test_tls1( gnutls_session state);
int test_tls1_1( gnutls_session state);
int test_tls1_1_fallback( gnutls_session state);
int test_tls_disable( gnutls_session state);
int test_rsa_pms( gnutls_session state);
int test_max_record_size( gnutls_session state);
int test_version_rollback( gnutls_session state);
int test_anonymous( gnutls_session state);
int test_unknown_ciphersuites( gnutls_session state);
int test_openpgp1( gnutls_session state);
int test_bye( gnutls_session state);
int test_certificate( gnutls_session state);
int test_server_cas( gnutls_session state);
int test_session_resume2( gnutls_session state);
int test_rsa_pms_version_check( gnutls_session session);
int test_version_oob( gnutls_session session);
int test_zlib( gnutls_session session);
int test_lzo( gnutls_session session);
int _test_srp_username_callback( gnutls_session session, unsigned int times,
	char** username, char** password);


