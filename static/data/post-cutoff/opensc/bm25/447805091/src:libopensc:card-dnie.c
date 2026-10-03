/**
 * card-dnie.c: Support for Spanish DNI electronico (DNIe card).
 *
 * Copyright (C) 2010 Juan Antonio Martinez <jonsito@terra.es>
 *
 * This work is derived from many sources at OpenSC Project site,
 * (see references) and the information made public for Spanish
 * Direccion General de la Policia y de la Guardia Civil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define __CARD_DNIE_C__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(ENABLE_OPENSSL) && defined(ENABLE_SM)	/* empty file without openssl or sm */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "opensc.h"
#include "cardctl.h"
#include "internal.h"
#include "cwa14890.h"
#include "cwa-dnie.h"

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#endif
#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif

#define MAX_RESP_BUFFER_SIZE 2048

/* default titles */
#define USER_CONSENT_TITLE "Confirm"

extern int dnie_read_file(
	sc_card_t * card,
	const sc_path_t * path,
	sc_file_t ** file,
	u8 ** buffer, size_t * length);

#define DNIE_CHIP_NAME "DNIe: Spanish eID card"
#define DNIE_CHIP_SHORTNAME "dnie"
#define DNIE_MF_NAME "Master.File"

/* default user consent program (if required) */
#define USER_CONSENT_CMD "/usr/bin/pinentry"

/**
 * SW internal apdu response table.
 *
 * Override APDU response error codes from iso7816.c to allow
 * handling of SM specific error
 */
static const struct sc_card_error dnie_errors[] = {
	{0x6688, SC_ERROR_SM, "Cryptographic checksum invalid"},
	{0x6987, SC_ERROR_SM, "Expected SM Data Object missing"},
	{0x6988, SC_ERROR_SM, "SM Data Object incorrect"},
	{0, 0, NULL}
};

/*
 * DNIe ATR info from DGP web page
 *
Tag Value Meaning
TS  0x3B  Direct Convention
T0  0x7F  Y1=0x07=0111; TA1,TB1 y TC1 present.
          K=0x0F=1111; 15 historical bytes
TA1 0x38  FI (Factor de conversión de la tasa de reloj) = 744
          DI (Factor de ajuste de la tasa de bits) = 12
          Máximo 8 Mhz.
TB1 0x00  Vpp (voltaje de programación) no requerido.
TC1 0x00  No se requiere tiempo de espera adicional.
H1  0x00  No usado
H2  0x6A  Datos de preexpedición. Diez bytes con identificación del expedidor.
H3  0x44  'D'
H4  0x4E  'N'
H5  0x49  'I'
H6  0x65  'e'
H7  Fabricante de la tecnología Match-on-Card incorporada.
    0x10  SAGEM
    0x20  SIEMENS
H8  0x02  Fabricante del CI: STMicroelectronics.
H9  0x4C
H10 0x34  Tipo de CI: 19WL34
H11 0x01  MSB de la version del SO: 1
H12 0x1v  LSB de la version del SO: 1v
H13 Fase del ciclo de vida .
    0x00  prepersonalización.
    0x01  personalización.
    0x03  usuario.
    0x0F  final.
H14 0xss
H15 0xss  Bytes de estado

H13-H15: 0x03 0x90 0x00 user phase: tarjeta operativa
H13-H15: 0x0F 0x65 0x81 final phase: tarjeta no operativa
*/

/**
 * ATR Table list.
 * OpenDNIe defines two ATR's for user and finalized card state
 */
static struct sc_atr_table dnie_atrs[] = {
	/* TODO: get ATR for uninitialized DNIe */
	{		/** card activated; normal operation state */
	 "3B:7F:00:00:00:00:6A:44:4E:49:65:00:00:00:00:00:00:03:90:00",
	 "FF:FF:00:FF:FF:FF:FF:FF:FF:FF:FF:00:00:00:00:00:00:FF:FF:FF",
	 DNIE_CHIP_SHORTNAME,
	 SC_CARD_TYPE_DNIE_USER,
	 0,
	 NULL},
	{		/** card finalized, unusable */
	 "3B:7F:00:00:00:00:6A:44:4E:49:65:00:00:00:00:00:00:0F:65:81",
	 "FF:FF:00:FF:FF:FF:FF:FF:FF:FF:FF:00:00:00:00:00:00:FF:FF:FF",
	 DNIE_CHIP_SHORTNAME,
	 SC_CARD_TYPE_DNIE_TERMINATED,
	 0,
	 NULL},
	{NULL, NULL, NULL, 0, 0, NULL}
};

/**
 * Messages used on user consent procedures
 */
const char *user_consent_title="Signature Requested";

#ifdef linux
const char *user_consent_message="Está a punto de realizar una firma electrónica con su clave de FIRMA del DNI electrónico. ¿Desea permitir esta operación?";
#else
const char *user_consent_message="Esta a punto de realizar una firma digital\ncon su clave de FIRMA del DNI electronico.\nDesea permitir esta operacion?";
#endif

#ifdef ENABLE_DNIE_UI
/**
 * Messages used on pinentry protocol
 */
char *user_consent_msgs[] = { "SETTITLE", "SETDESC", "CONFIRM", "BYE" };

#if !defined(__APPLE__) && !defined(_WIN32)
/**
 * Do fgets() without interruptions.
 *
 * Retry the operation if it is interrupted, such as with receiving an alarm.
 *
 * @param s Buffer receiving the data
 * @param size Size of the buffer
 * @param stream Stream to read
 * @return s on success, NULL on error
 */
static char *nointr_fgets(char *s, int size, FILE *stream)
{
	while (fgets(s, size, stream) == NULL) {
		if (feof(stream) || errno != EINTR)
			return NULL;
	}
	return s;
}
#endif

/**
 * Ask for user consent.
 *
 * Check for user consent configuration,
 * Invoke proper gui app and check result
 *
 * @param card pointer to sc_card structure
 * @param title Text to appear in the window header
 * @param text Message to show to the user
 * @return SC_SUCCESS on user consent OK , else error code
 */
int dnie_ask_user_consent(struct sc_card * card, const char *title, const char *message)
{
#ifdef __APPLE__
	CFOptionFlags result;  /* result code from the message box */
	/* convert the strings from char* to CFStringRef */
	CFStringRef header_ref; /* to store title */
	CFStringRef message_ref; /* to store message */
#endif
#if !defined(__APPLE__) && !defined(_WIN32)
	pid_t pid;
	FILE *fin=NULL;
	FILE *fout=NULL;	/* to handle pipes as streams */
	struct stat st_file;	/* to verify that executable exists */
	int srv_send[2];	/* to send data from server to client */
	int srv_recv[2];	/* to receive data from client to server */
	char outbuf[1024];	/* to compose and send messages */
	char buf[1024];		/* to store client responses */
	int n = 0;		/* to iterate on to-be-sent messages */
#endif
	int res = SC_ERROR_INTERNAL;	/* by default error :-( */
	char *msg = NULL;	/* to mark errors */

	if ((card == NULL) || (card->ctx == NULL))
		return SC_ERROR_INVALID_ARGUMENTS;
	LOG_FUNC_CALLED(card->ctx);

	if ((title==NULL) || (message==NULL))
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_ARGUMENTS);

	if (GET_DNIE_UI_CTX(card).user_consent_enabled == 0
			|| card->ctx->flags & SC_CTX_FLAG_DISABLE_POPUPS) {
		sc_log(card->ctx,
		       "User Consent or popups are disabled in configuration file");
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	}
#ifdef _WIN32
	/* in Windows, do not use pinentry, but MessageBox system call */
	res = MessageBox (
		NULL,
		TEXT(message),
		TEXT(title),
		MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2 | MB_APPLMODAL
		);
	if ( res == IDOK )
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	LOG_FUNC_RETURN(card->ctx, SC_ERROR_NOT_ALLOWED);
#elif __APPLE__
	/* Also in Mac OSX use native functions */

	/* convert the strings from char* to CFStringRef */
	header_ref = CFStringCreateWithCString( NULL, title, strlen(title) );
	message_ref = CFStringCreateWithCString( NULL,message, strlen(message) );

	/* Display user notification alert */
	CFUserNotificationDisplayAlert(
		0, /* no timeout */
		kCFUserNotificationNoteAlertLevel,  /* Alert level */
		NULL,	/* IconURL, use default, you can change */
			/* it depending message_type flags */
		NULL,	/* SoundURL (not used) */
		NULL,	/* localization of strings */
		header_ref,	/* header. Cannot be null */
		message_ref,	/* message text */
		CFSTR("Cancel"), /* default ( "OK" if null) button text */
		CFSTR("OK"), /* second button title */
                NULL, /* third button title, null--> no other button */
		&result /* response flags */
	);

	/* Clean up the strings */
	CFRelease( header_ref );
        CFRelease( message_ref );
	/* Return 0 only if "OK" is selected */
	if( result == kCFUserNotificationAlternateResponse )
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	LOG_FUNC_RETURN(card->ctx, SC_ERROR_NOT_ALLOWED);
#else
	/* just a simple bidirectional pipe+fork+exec implementation */
	/* In a pipe, xx[0] is for reading, xx[1] is for writing */
	if (pipe(srv_send) < 0) {
		msg = "pipe(srv_send)";
		goto do_error;
	}
	if (pipe(srv_recv) < 0) {
		msg = "pipe(srv_recv)";
		goto do_error;
	}
	pid = fork();
	switch (pid) {
	case -1:		/* error  */
		msg = "fork()";
		goto do_error;
	case 0:		/* child  */
		/* make our pipes, our new stdin & stderr, closing older ones */
		dup2(srv_send[0], STDIN_FILENO);	/* map srv send for input */
		dup2(srv_recv[1], STDOUT_FILENO);	/* map srv_recv for output */
		/* once dup2'd pipes are no longer needed on client; so close */
		close(srv_send[0]);
		close(srv_send[1]);
		close(srv_recv[0]);
		close(srv_recv[1]);
		/* check that user_consent_app exists. TODO: check if executable */
		res = stat(GET_DNIE_UI_CTX(card).user_consent_app, &st_file);
		if (res != 0) {
			sc_log(card->ctx, "Invalid pinentry application: %s\n",
					GET_DNIE_UI_CTX(card).user_consent_app);
		} else {
			/* call exec() with proper user_consent_app from configuration */
			/* if ok should never return */
			execlp(GET_DNIE_UI_CTX(card).user_consent_app, GET_DNIE_UI_CTX(card).user_consent_app, (char *)NULL);
			sc_log(card->ctx, "execlp() error");
		}
		abort();
	default:		/* parent */
		/* Close the pipe ends that the child uses to read from / write to
		 * so when we close the others, an EOF will be transmitted properly.
		 */
		close(srv_send[0]);
		close(srv_recv[1]);
		/* use iostreams to take care on newlines and text based data */
		fin = fdopen(srv_recv[0], "r");
		if (fin == NULL) {
			msg = "fdopen(in)";
			goto do_error;
		}
		fout = fdopen(srv_send[1], "w");
		if (fout == NULL) {
			msg = "fdopen(out)";
			goto do_error;
		}
		/* read and ignore first line */
		if (nointr_fgets(buf, sizeof(buf), fin) == NULL) {
			res = SC_ERROR_INTERNAL;
			msg = "nointr_fgets() Unexpected IOError/EOF";
			goto do_error;
		}
		for (n = 0; n<4; n++) {
			char *pt;
			if (n==0) snprintf(outbuf, sizeof outbuf,"%s %s\n",user_consent_msgs[0],title);
			else if (n==1) snprintf(outbuf, sizeof outbuf,"%s %s\n",user_consent_msgs[1],message);
			else snprintf(outbuf, sizeof outbuf,"%s\n",user_consent_msgs[n]);
			/* send message */
			fputs(outbuf, fout);
			fflush(fout);
			/* get response */
			pt=nointr_fgets(buf, sizeof(buf), fin);
			if (pt==NULL) {
				res = SC_ERROR_INTERNAL;
				msg = "nointr_fgets() Unexpected IOError/EOF";
				goto do_error;
			}
			if (strstr(buf, "OK") == NULL) {
				res = SC_ERROR_NOT_ALLOWED;
				msg = "fail/cancel";
				goto do_error;
			}
		}
	}			/* switch */
	/* arriving here means signature has been accepted by user */
	res = SC_SUCCESS;
	msg = NULL;
do_error:
	/* close out channel to force client receive EOF and also die */
	if (fout != NULL) fclose(fout);
	if (fin != NULL) fclose(fin);
#endif
	if (msg != NULL)
		sc_log(card->ctx, "%s", msg);
	LOG_FUNC_RETURN(card->ctx, res);
}

#endif				/* ENABLE_DNIE_UI */

/**
 * DNIe specific card driver operations
 */
static struct sc_card_operations dnie_ops;

/**
 * Local copy of iso7816 card driver operations
 */
static struct sc_card_operations *iso_ops = NULL;

/**
 * Module definition for OpenDNIe card driver
 */
static sc_card_driver_t dnie_driver = {
	DNIE_CHIP_NAME, /**< Full name for DNIe card driver */
	DNIE_CHIP_SHORTNAME, /**< Short name for DNIe card driver */
	&dnie_ops,	/**< pointer to dnie_ops (DNIe card driver operations) */
	dnie_atrs,	/**< List of card ATR's handled by this driver */
	0,		/**< (natrs) number of atr's to check for this driver */
	NULL		/**< (dll) Card driver module (on DNIe is null) */
};

/************************** card-dnie.c internal functions ****************/

/**
 * Parse configuration file for dnie parameters.
 *
 * DNIe card driver has two main parameters:
 * - The name of the user consent Application to be used in Linux. This application should be any of pinentry-xxx family
 * - A flag to indicate if user consent is to be used in this driver. If false, the user won't be prompted for confirmation on signature operations
 *
 * @See ../../etc/opensc.conf for details
 * @param card Pointer to card structure
 * @param ui_context Pointer to ui_context structure to store data into
 * @return SC_SUCCESS (should return no errors)
 *
 * TODO: Code should be revised in order to store user consent info
 * in a card-independent way at configuration file
 */
#ifdef ENABLE_DNIE_UI
static int dnie_get_environment(
	sc_card_t * card,
	ui_context_t * ui_context)
{
	int i;
	scconf_block **blocks, *blk;
	sc_context_t *ctx;
	/* set default values */
	ui_context->user_consent_app = USER_CONSENT_CMD;
	ui_context->user_consent_enabled = 1;
	/* look for sc block in opensc.conf */
	ctx = card->ctx;
	for (i = 0; ctx->conf_blocks[i]; i++) {
		blocks = scconf_find_blocks(ctx->conf, ctx->conf_blocks[i],
				"card_driver", "dnie");
		if (!blocks)
			continue;
		blk = blocks[0];
		free(blocks);
		if (blk == NULL)
			continue;
		/* fill private data with configuration parameters */
		ui_context->user_consent_app =	/* def user consent app is "pinentry" */
		    (char *)scconf_get_str(blk, "user_consent_app",
					   USER_CONSENT_CMD);
		ui_context->user_consent_enabled =	/* user consent is enabled by default */
		    scconf_get_bool(blk, "user_consent_enabled", 1);
	}
	return SC_SUCCESS;
}
#endif

/************************** cardctl defined operations *******************/

/**
 * Generate a public/private key pair.
 *
 * Manual says that generate_keys() is a reserved operation; that is:
 * only can be done at DGP offices. But several authors talk about
 * this operation is available also outside. So need to test :-)
 * Notice that write operations are not supported, so we can't use
 * created keys to generate and store new certificates into the card.
 * TODO: copy code from card-jcop.c::jcop_generate_keys()
 * @param card pointer to card info data
 * @param data where to store function results
 * @return SC_SUCCESS if ok, else error code
 */
static int dnie_generate_key(sc_card_t * card, void *data)
{
	int result = SC_ERROR_NOT_SUPPORTED;
	if ((card == NULL) || (data == NULL))
		return SC_ERROR_INVALID_ARGUMENTS;
	LOG_FUNC_CALLED(card->ctx);
	/* TODO: write dnie_generate_key() */
	LOG_FUNC_RETURN(card->ctx, result);
}

/**
 * Analyze a buffer looking for provided data pattern.
 *
 * Commodity function for dnie_get_info() that searches a byte array
 * in provided buffer
 *
 * @param card pointer to card info data
 * @param pat data pattern to find in buffer
 * @param buf where to look for pattern
 * @param len buffer length
 * @return retrieved value or NULL if pattern not found
 * @see dnie_get_info()
 */
static char *findPattern(u8 *pat, u8 *buf, size_t len)
{
	char *res = NULL;
	u8 *from = buf;
	int size = 0;
	/* Locate pattern. Assume pattern length=6 */
	for ( from = buf; from < buf+len-6; from++) {
		if (memcmp(from,pat,6) == 0 ) goto data_found;
	}
	/* arriving here means pattern not found */
	return NULL;

data_found:
	/* assume length is less than 128 bytes, so is coded in 1 byte */
	size = 0x000000ff & (int) *(from+6);
	if ( size == 0 ) return NULL; /* empty data */
	res = calloc( size+1, sizeof(char) );
	if ( res == NULL) return NULL; /* calloc() error */
	memcpy(res,from+7,size);
	return res;
}

/**
 * Retrieve name, surname, and DNIe number.
 *
 * This is done by mean of reading and parsing CDF file
 * at address 3F0050156004
 * No need to enter pin nor use Secure Channel
 *
 * Notice that this is done by mean of a dirty trick: instead
 * of parsing ASN1 data on EF(CDF),
 * we look for desired OID patterns in binary array
 *
 * @param card pointer to card info data
 * @param data where to store function results (number,name,surname,idesp,version)
 * @return SC_SUCCESS if ok, else error code
 */
static int dnie_get_info(sc_card_t * card, char *data[])
{
	sc_file_t *file = NULL;
        sc_path_t path;
        u8 *buffer = NULL;
	size_t bufferlen = 0;
	char *msg = NULL;
	u8 SerialNumber [] = { 0x06, 0x03, 0x55, 0x04, 0x05, 0x13 };
	u8 Name [] = { 0x06, 0x03, 0x55, 0x04, 0x04, 0x0C };
	u8 GivenName [] = { 0x06, 0x03, 0x55, 0x04, 0x2A, 0x0C };
	int res = SC_ERROR_NOT_SUPPORTED;

        if ((card == NULL) || (data == NULL))
                return SC_ERROR_INVALID_ARGUMENTS;
        LOG_FUNC_CALLED(card->ctx);

	/* phase 1: get DNIe number, Name and GivenName */

	/* read EF(CDF) at 3F0050156004 */
	sc_format_path("3F0050156004", &path);
	res = dnie_read_file(card, &path, &file, &buffer, &bufferlen);
	if (res != SC_SUCCESS) {
		msg = "Cannot read EF(CDF)";
		goto get_info_end;
	}
	/* locate OID 2.5.4.5 (SerialNumber) - DNIe number*/
	data[0]= findPattern(SerialNumber,buffer,bufferlen);
	/* locate OID 2.5.4.4 (Name)         - Apellidos */
	data[1]= findPattern(Name,buffer,bufferlen);
	/* locate OID 2.5.4.42 (GivenName)   - Nombre */
	data[2]= findPattern(GivenName,buffer,bufferlen);
	if ( ! data[0] || !data[1] || !data[2] ) {
		res = SC_ERROR_INVALID_DATA;
		msg = "Cannot retrieve info from EF(CDF)";
		goto get_info_end;
        }

	/* phase 2: get IDESP */
	sc_format_path("3F000006", &path);
	sc_file_free(file);
	file = NULL;
	if (buffer) {
		free(buffer);
		buffer=NULL;
		bufferlen=0;
	}
	res = dnie_read_file(card, &path, &file, &buffer, &bufferlen);
	if (res != SC_SUCCESS) {
		data[3]=NULL;
		goto get_info_ph3;
	}
	data[3]=calloc(bufferlen+1,sizeof(char));
	if ( !data[3] ) {
		msg = "Cannot allocate memory for IDESP data";
		res = SC_ERROR_OUT_OF_MEMORY;
		goto get_info_end;
	}
	memcpy(data[3],buffer,bufferlen);

get_info_ph3:
	/* phase 3: get DNIe software version */
	sc_format_path("3F002F03", &path);
	sc_file_free(file);
	file = NULL;
	if (buffer) {
		free(buffer);
		buffer=NULL;
		bufferlen=0;
	}
	/*
	* Some old DNIe cards seems not to include SW version file,
 	* so let this code fail without notice
 	*/
	res = dnie_read_file(card, &path, &file, &buffer, &bufferlen);
	if (res != SC_SUCCESS) {
		msg = "Cannot read DNIe Version EF";
		data[4]=NULL;
		res = SC_SUCCESS; /* let function return successfully */
		goto get_info_end;
	}
	data[4]=calloc(bufferlen+1,sizeof(char));
	if ( !data[4] ) {
		msg = "Cannot allocate memory for DNIe Version data";
		res = SC_ERROR_OUT_OF_MEMORY;
		goto get_info_end;
	}
	memcpy(data[4],buffer,bufferlen);

	/* arriving here means ok */
	res = SC_SUCCESS;
	msg = NULL;

get_info_end:
	sc_file_free(file);
	file = NULL;
	if (buffer) {
		free(buffer);
		buffer=NULL;
		bufferlen=0;
	}
	if (msg)
		sc_log(card->ctx, "%s", msg);
        LOG_FUNC_RETURN(card->ctx, res);
}

/**
 * Retrieve serial number (7 bytes) from card.
 *
 * This is done by mean of an special APDU command described
 * in the DNIe Reference Manual
 *
 * @param card pointer to card description
 * @param serial where to store data retrieved
 * @return SC_SUCCESS if ok; else error code
 */
static int dnie_get_serialnr(sc_card_t * card, sc_serial_number_t * serial)
{
	int result;
	sc_apdu_t apdu;
	u8 rbuf[MAX_RESP_BUFFER_SIZE];
	if ((card == NULL) || (card->ctx == NULL) || (serial == NULL))
		return SC_ERROR_INVALID_ARGUMENTS;

	LOG_FUNC_CALLED(card->ctx);
	if (card->type != SC_CARD_TYPE_DNIE_USER)
		return SC_ERROR_NOT_SUPPORTED;
	/* if serial number is cached, use it */
	if (card->serialnr.len) {
		memcpy(serial, &card->serialnr, sizeof(*serial));
		sc_log_hex(card->ctx, "Serial Number (cached)", serial->value, serial->len);
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	}
	/* not cached, retrieve it by mean of an APDU */
	/* official driver read 0x11 bytes, but only uses 7. Manual says just 7 (for le) */
	dnie_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xb8, 0x00, 0x00, 0x07, 0,
					rbuf, sizeof(rbuf), NULL, 0);
	apdu.cla = 0x90;	/* proprietary cmd */
	/* send apdu */
	result = sc_transmit_apdu(card, &apdu);
	if (result != SC_SUCCESS) {
		LOG_TEST_RET(card->ctx, result, "APDU transmit failed");
	}
	if (apdu.sw1 != 0x90 || apdu.sw2 != 0x00)
		return SC_ERROR_INTERNAL;
	/* cache serial number */
	memcpy(card->serialnr.value, apdu.resp, 7 * sizeof(u8));
	card->serialnr.len = 7 * sizeof(u8);
	/* TODO: fill Issuer Identification Number data with proper (ATR?) info */
	/*
	   card->serialnr.iin.mii=;
	   card->serialnr.iin.country=;
	   card->serialnr.iin.issuer_id=;
	 */
	/* copy and return serial number */
	memcpy(serial, &card->serialnr, sizeof(*serial));
	sc_log_hex(card->ctx, "Serial Number (apdu)", serial->value, serial->len);
	LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
}

/**
 * Remove the binary data in the cache.
 *
 * It frees memory if allocated and resets pointer and length.
 * It only touches the private binary cache variables, not the sc_card information.
 *
 * @param data pointer to dnie private data
 */
static void dnie_clear_cache(dnie_private_data_t * data)
{
	if (data == NULL) return;
	if (data->cache != NULL)
		free(data->cache);
	data->cache = NULL;
	data->cachelen = 0;
}

/**
 * Set sc_card flags according to DNIe requirements.
 *
 * Used in card initialization.
 *
 * @param card pointer to card data
 */
static void init_flags(struct sc_card *card)
{
	unsigned long algoflags;
	/* set up flags according documentation */
	card->name = DNIE_CHIP_SHORTNAME;
	card->cla = 0x00;	/* default APDU class (interindustry) */
	card->caps |= SC_CARD_CAP_RNG;	/* we have a random number generator */
	card->max_send_size = (255 - 12);	/* manual says 255, but we need 12 extra bytes when encoding */
	card->max_recv_size = 255;

	/* RSA Support with PKCS1.5 padding */
	algoflags = SC_ALGORITHM_RSA_HASH_NONE | SC_ALGORITHM_RSA_PAD_PKCS1;
	_sc_card_add_rsa_alg(card, 1024, algoflags, 0);
	_sc_card_add_rsa_alg(card, 1920, algoflags, 0);
	_sc_card_add_rsa_alg(card, 2048, algoflags, 0);
}

/**************************** sc_card_operations **********************/

/* Generic operations */

/**
 * Check if provided card can be handled by OpenDNIe.
 *
 * Called in sc_connect_card().  Must return 1, if the current
 * card can be handled with this driver, or 0 otherwise.  ATR
 * field of the sc_card struct is filled in before calling
 * this function.
 * do not declare static, as used by pkcs15-dnie module
 *
 * @param card Pointer to card structure
 * @return on card matching 0 if not match; negative return means error
 */
int dnie_match_card(struct sc_card *card)
{
	int result = 0;
	int matched = -1;
	LOG_FUNC_CALLED(card->ctx);
	matched = _sc_match_atr(card, dnie_atrs, &card->type);
	result = (matched >= 0) ? 1 : 0;
	LOG_FUNC_RETURN(card->ctx, result);
}

static int dnie_sm_free_wrapped_apdu(struct sc_card *card,
		struct sc_apdu *plain, struct sc_apdu **sm_apdu)
{
	struct sc_context *ctx = card->ctx;
	cwa_provider_t *provider = NULL;
	int rv = SC_SUCCESS;

	LOG_FUNC_CALLED(ctx);
	provider = GET_DNIE_PRIV_DATA(card)->cwa_provider;
	if (!sm_apdu)
		LOG_FUNC_RETURN(ctx, SC_ERROR_INVALID_ARGUMENTS);
	if (!(*sm_apdu))
		LOG_FUNC_RETURN(ctx, SC_SUCCESS);

	if ((*sm_apdu) != plain) {
		rv = cwa_decode_response(card, provider, *sm_apdu);
		if (plain && rv == SC_SUCCESS) {
			if (plain->resp) {
				/* copy the response into the original resp buffer */
				if ((*sm_apdu)->resplen <= plain->resplen) {
					memcpy(plain->resp, (*sm_apdu)->resp, (*sm_apdu)->resplen);
					plain->resplen = (*sm_apdu)->resplen;
				} else {
					sc_log(card->ctx, "Invalid initial length,"
							" needed %"SC_FORMAT_LEN_SIZE_T"u bytes"
							" but has %"SC_FORMAT_LEN_SIZE_T"u",
							(*sm_apdu)->resplen, plain->resplen);
					rv = SC_ERROR_BUFFER_TOO_SMALL;
				}
			}
			plain->sw1 = (*sm_apdu)->sw1;
			plain->sw2 = (*sm_apdu)->sw2;
		}
		if (plain == NULL || (*sm_apdu)->data != plain->data)
			free((unsigned char *) (*sm_apdu)->data);
		if (plain == NULL || (*sm_apdu)->resp != plain->resp)
			free((*sm_apdu)->resp);
		free(*sm_apdu);
	}
	*sm_apdu = NULL;

	LOG_FUNC_RETURN(ctx, rv);
}

static int dnie_sm_get_wrapped_apdu(struct sc_card *card,
		struct sc_apdu *plain, struct sc_apdu **sm_apdu)
{
	struct sc_context *ctx = card->ctx;
	cwa_provider_t *provider = NULL;
	int rv = SC_SUCCESS;

	LOG_FUNC_CALLED(ctx);
	if (!plain || !sm_apdu)
		LOG_FUNC_RETURN(ctx, SC_ERROR_INVALID_ARGUMENTS);

	provider = GET_DNIE_PRIV_DATA(card)->cwa_provider;

	if (((plain->cla & 0x0C) == 0) && (plain->ins != 0xC0)) {
		*sm_apdu = calloc(1, sizeof(struct sc_apdu));
		if (!(*sm_apdu))
			return SC_ERROR_OUT_OF_MEMORY;

		rv = cwa_encode_apdu(card, provider, plain, *sm_apdu);

		if (rv != SC_SUCCESS) {
			dnie_sm_free_wrapped_apdu(card, plain, sm_apdu);
		}
	} else
		*sm_apdu = plain;

	LOG_FUNC_RETURN(ctx, rv);
}

/**
 * OpenDNIe card structures initialization.
 *
 * Called when ATR of the inserted card matches an entry in ATR
 * table.  May return SC_ERROR_INVALID_CARD to indicate that
 * the card cannot be handled with this driver.
 *
 * @param card Pointer to card structure
 * @return SC_SUCCES if ok; else error code
 */
static int dnie_init(struct sc_card *card)
{
	int res = SC_SUCCESS;
	sc_context_t *ctx = card->ctx;
	cwa_provider_t *provider = NULL;

	LOG_FUNC_CALLED(ctx);

	/* if recognized as terminated DNIe card, return error */
	if (card->type == SC_CARD_TYPE_DNIE_TERMINATED)
	    LOG_TEST_RET(card->ctx, SC_ERROR_INVALID_CARD, "DNIe card is terminated.");

	/* create and initialize cwa-dnie provider*/
	provider = dnie_get_cwa_provider(card);
	if (!provider)
	    LOG_TEST_RET(card->ctx, SC_ERROR_INTERNAL, "Error initializing cwa-dnie provider");

	/** Secure messaging initialization section **/
	memset(&(card->sm_ctx), 0, sizeof(sm_context_t));
	card->sm_ctx.ops.get_sm_apdu = dnie_sm_get_wrapped_apdu;
	card->sm_ctx.ops.free_sm_apdu = dnie_sm_free_wrapped_apdu;
	card->sm_ctx.sm_mode = SM_MODE_NONE;

	res = cwa_create_secure_channel(card, provider, CWA_SM_OFF);
	if (res < 0)
		free(provider);
	LOG_TEST_RET(card->ctx, res, "Failure resetting CWA secure channel.");

	/* initialize private data */
	card->drv_data = calloc(1, sizeof(dnie_private_data_t));
	if (card->drv_data == NULL) {
		free(provider);
	    LOG_TEST_RET(card->ctx, SC_ERROR_OUT_OF_MEMORY, "Could not allocate DNIe private data.");
	}

#ifdef ENABLE_DNIE_UI
	/* read environment from configuration file */
	res = dnie_get_environment(card, &(GET_DNIE_UI_CTX(card)));
	if (res != SC_SUCCESS) {
		free(card->drv_data);
		free(provider);
		LOG_TEST_RET(card->ctx, res, "Failure reading DNIe environment.");
	}
#endif

	init_flags(card);

	GET_DNIE_PRIV_DATA(card)->cwa_provider = provider;

	LOG_FUNC_RETURN(card->ctx, res);
}

/**
 * De-initialization routine.
 *
 * Called when the card object is being freed.  finish() has to
 * deallocate all possible private data.
 *
 * @param card Pointer to card driver data structure
 * @return SC_SUCCESS if ok; else error code
 */
static int dnie_finish(struct sc_card *card)
{
	int result = SC_SUCCESS;
	LOG_FUNC_CALLED(card->ctx);
	dnie_clear_cache(GET_DNIE_PRIV_DATA(card));
	/* disable sm channel if established */
	result = cwa_create_secure_channel(card, GET_DNIE_PRIV_DATA(card)->cwa_provider, CWA_SM_OFF);
	free(GET_DNIE_PRIV_DATA(card)->cwa_provider);
	free(card->drv_data);
	LOG_FUNC_RETURN(card->ctx, result);
}

/* ISO 7816-4 functions */

/**
 * Check whether data are compressed.
 *
 * @param card pointer to sc_card_t structure
 * @param from buffer to get data from
 * @param len buffer length
 * @return 1 if data are compressed, 0 otherwise; len points to expected length of decompressed data
 */

static int dnie_is_compressed(sc_card_t * card, u8 * from, size_t len)
{
#ifdef ENABLE_ZLIB
	size_t uncompressed = 0L;
	size_t compressed = 0L;

	if (!card || !card->ctx || !from || !len)
		return 0;
	LOG_FUNC_CALLED(card->ctx);

	/* if data size not enough for compression header assume uncompressed */
