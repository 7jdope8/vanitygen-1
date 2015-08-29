#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include "winglue.h"
#endif

#include "pattern.h"
#include "util.h"

const char *version = VANITYGEN_VERSION;


static void
usage(const char *progname)
{
	fprintf(stderr,
"Vanitygen keyconv %s\n"
"Usage: %s [-8] [-e|-E <password>] [-c <key>] [<key>]\n"
"-G            Generate a key pair and output the full public key\n"
"-8            Output key in PKCS#8 form\n"
"-C 		   Generate Compressed key\n"
"-e            Encrypt output key, prompt for password\n"
"-X <version>  Generate address with the given version\n"
"-p <privtyp>  The priv-type belonging to the version, default <version>+128\n"
"              default is used when ommitted, can only be used combined with -X\n"
"-E <password> Encrypt output key with <password> (UNSAFE)\n"
"-c <key>      Combine private key parts to make complete private key\n"
"-v            Verbose output\n",
		version, progname);
}

int
main(int argc, char **argv)
{
	char pwbuf[128];
	char ecprot[128];
	char pbuf[1024];
	char cbuf[1024];
	const char *key_in;
	const char *pass_in = NULL;
	const char *key2_in = NULL;
	EC_KEY *pkey;
	int parameter_group = -1;
	int compressedkey = 0;
	int privtype, addrtype, newprivtype=0;
	int pkcs8 = 0;
	int pass_prompt = 0;
	int verbose = 0;
	int generate = 0;
	int versionoverride=0, privtypeoverride=0;
	int opt;
	int res;

	while ((opt = getopt(argc, argv, "8E:ec:vGX:p:C")) != -1) {
		switch (opt) {
		case 'C':
			compressedkey = 1;
			break;
		case '8':
			pkcs8 = 1;
			break;
		case 'E':
			if (pass_prompt) {
				usage(argv[0]);
				return 1;
			}
			pass_in = optarg;
			if (!vg_check_password_complexity(pass_in, 1))
				fprintf(stderr,
					"WARNING: Using weak password\n");
			break;
		case 'e':
			if (pass_in) {
				usage(argv[0]);
				return 1;
			}
			pass_prompt = 1;
			break;
		case 'c':
			key2_in = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'G':
			generate = 1;
			break;
		case 'X':
			addrtype = atoi(optarg);
			privtype = 128 + addrtype;
			versionoverride = 1;
			break;
		case 'p':
			newprivtype = atoi(optarg);
			privtypeoverride = 1;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	OpenSSL_add_all_algorithms();

	if (versionoverride==0){
		addrtype = 0;
		privtype = 128;
	}

	pkey = EC_KEY_new_by_curve_name(NID_secp256k1);
	if (!(versionoverride)){
		switch (privtype) {
		case 128: addrtype = 0; break;
		case 239: addrtype = 111; break;
		default:  addrtype = 0; break;
		}
	}
	if (privtypeoverride && versionoverride){
		privtype = newprivtype;
	}
	if (generate) {
		unsigned char *pend = (unsigned char *) pbuf;
		unsigned char *cpub = (unsigned char *) cbuf;
		EC_KEY_generate_key(pkey);
		res = i2o_ECPublicKey(pkey, &pend);
		fprintf(stderr, "---------------------------------------------------------\n");
		fprintf(stderr, "Results:\n");
		fprintf(stderr, "Pubkey (hex):\n\t");
		dumphex((unsigned char *)pbuf, res);
		fprintf(stderr, "Privkey (hex):\n\t");
		dumpbn(EC_KEY_get0_private_key(pkey));
		vg_encode_address(EC_KEY_get0_public_key(pkey),
				  EC_KEY_get0_group(pkey),
				  addrtype, ecprot);
		printf("Address: %s\n", ecprot);
		vg_encode_privkey(pkey, privtype, ecprot);
		printf("WiF key: %s\n", ecprot);
		if (compressedkey == 1){
			printf("Compressed Results:\n");
			res = EC_POINT_point2oct(EC_KEY_get0_group(pkey), EC_KEY_get0_public_key(pkey),
									 POINT_CONVERSION_COMPRESSED,
									 cpub, 33, NULL);
			printf("Pubkey Compressed (hex):\n\t");
			dumphex((unsigned char *)cpub, res);
			vg_encode_address_compressed(EC_KEY_get0_public_key(pkey),
					  EC_KEY_get0_group(pkey),
					  addrtype, ecprot);
			printf("Address Compressed:\n\t%s\n", ecprot);
			vg_encode_privkey_compressed(pkey, privtype, ecprot);
			printf("Wif key Compressed:\n\t%s\n", ecprot);
			fprintf(stderr, "---------------------------------------------------------\n");
		}

		return 0;
	}

	if (optind >= argc) {
		res = fread(pbuf, 1, sizeof(pbuf) - 1, stdin);
		pbuf[res] = '\0';
		key_in = pbuf;
	} else {
		key_in = argv[optind];
	}

	res = vg_decode_privkey_any(pkey, &privtype, key_in, NULL);
	if (res < 0) {
		if (EVP_read_pw_string(pwbuf, sizeof(pwbuf),
				       "Enter import password:", 0) ||
		    !vg_decode_privkey_any(pkey, &privtype, key_in, pwbuf))
			return 1;
	}

	if (!res) {
		fprintf(stderr, "ERROR: Unrecognized key format\n");
		return 1;
	}

	if (key2_in) {
		BN_CTX *bnctx;
		BIGNUM bntmp, bntmp2;
		EC_KEY *pkey2;

		pkey2 = EC_KEY_new_by_curve_name(NID_secp256k1);
		res = vg_decode_privkey_any(pkey2, &privtype, key2_in, NULL);
		if (res < 0) {
			if (EVP_read_pw_string(pwbuf, sizeof(pwbuf),
					       "Enter import password:", 0) ||
			    !vg_decode_privkey_any(pkey2, &privtype,
						   key2_in, pwbuf))
				return 1;
		}

		if (!res) {
			fprintf(stderr, "ERROR: Unrecognized key format\n");
			return 1;
		}
		BN_init(&bntmp);
		BN_init(&bntmp2);
		bnctx = BN_CTX_new();
		EC_GROUP_get_order(EC_KEY_get0_group(pkey), &bntmp2, NULL);
		BN_mod_add(&bntmp,
			   EC_KEY_get0_private_key(pkey),
			   EC_KEY_get0_private_key(pkey2),
			   &bntmp2,
			   bnctx);
		vg_set_privkey(&bntmp, pkey);
		EC_KEY_free(pkey2);
		BN_clear_free(&bntmp);
		BN_clear_free(&bntmp2);
		BN_CTX_free(bnctx);
	}

	if (pass_prompt) {
		res = EVP_read_pw_string(pwbuf, sizeof(pwbuf),
					 "Enter password:", 1);
		if (res)
			return 1;
		pass_in = pwbuf;
		if (!vg_check_password_complexity(pwbuf, 1))
			fprintf(stderr, "WARNING: Using weak password\n");
	}

	if (verbose) {
		unsigned char *pend = (unsigned char *) pbuf;
		res = i2o_ECPublicKey(pkey, &pend);
		fprintf(stderr, "Pubkey (hex): ");
		dumphex((unsigned char *)pbuf, res);
		fprintf(stderr, "Privkey (hex): ");
		dumpbn(EC_KEY_get0_private_key(pkey));
	}
			
	if (pkcs8) {
		res = vg_pkcs8_encode_privkey(pbuf, sizeof(pbuf),
					      pkey, pass_in);
		if (!res) {
			fprintf(stderr,
				"ERROR: Could not encode private key\n");
			return 1;
		}
		printf("%s", pbuf);
	}

	else if (pass_in) {
		res = vg_protect_encode_privkey(ecprot, pkey, privtype,
						parameter_group, pass_in);

		if (!res) {
			fprintf(stderr, "ERROR: could not password-protect "
				"private key\n");
			return 1;
		}

		vg_encode_address(EC_KEY_get0_public_key(pkey),
				  EC_KEY_get0_group(pkey),
				  addrtype, pwbuf);
		printf("Address: %s\n", pwbuf);
		printf("Protkey: %s\n", ecprot);
	}

	else {
		unsigned char *cpub = (unsigned char *) cbuf;
		unsigned char *pend = (unsigned char *) pbuf;

		res = i2o_ECPublicKey(pkey, &pend);
		fprintf(stderr, "---------------------------------------------------------\n");
		fprintf(stderr, "Results:\n");
		fprintf(stderr, "Pubkey (hex):\n\t");
		dumphex((unsigned char *)pbuf, res);
		fprintf(stderr, "Privkey (hex):\n\t");
		dumpbn(EC_KEY_get0_private_key(pkey));

		vg_encode_address(EC_KEY_get0_public_key(pkey),
				  EC_KEY_get0_group(pkey),
				  addrtype, ecprot);
		printf("Address: %s\n", ecprot);
		vg_encode_privkey(pkey, privtype, ecprot);
		printf("Wif key: %s\n", ecprot);
		if (compressedkey == 1){
			printf("Compressed Results:\n");
			res = EC_POINT_point2oct(EC_KEY_get0_group(pkey), EC_KEY_get0_public_key(pkey),
									 POINT_CONVERSION_COMPRESSED,
									 cpub, 33, NULL);
			printf("Pubkey Compressed (hex):\n\t");
			dumphex((unsigned char *)cpub, res);
			vg_encode_address_compressed(EC_KEY_get0_public_key(pkey),
					  EC_KEY_get0_group(pkey),
					  addrtype, ecprot);
			printf("Address Compressed:\n\t%s\n", ecprot);
			vg_encode_privkey_compressed(pkey, privtype, ecprot);
			printf("Wif key Compressed:\n\t%s\n", ecprot);
			fprintf(stderr, "---------------------------------------------------------\n");
		}

	}

	OPENSSL_cleanse(pwbuf, sizeof(pwbuf));

	EC_KEY_free(pkey);
	return 0;
}
