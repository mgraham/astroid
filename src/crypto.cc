# include <glib.h>
# include <gmime/gmime.h>

# include <string>

# include <boost/algorithm/string.hpp>

# include "astroid.hh"
# include "log.hh"
# include "config.hh"
# include "crypto.hh"
# include "utils/address.hh"

using std::endl;

namespace Astroid {
  Crypto::Crypto (ustring _protocol) {
    using std::endl;
    config = astroid->config ("crypto");
    gpgpath = ustring (config.get<std::string> ("gpg.path"));
    auto_key_retrieve = config.get<bool> ("gpg.auto_key_retrieve");
    always_trust = config.get<bool> ("gpg.always_trust");

    log << debug << "crypto: gpg: " << gpgpath << endl;

    protocol = _protocol.lowercase ();

    if ((protocol == "application/pgp-encrypted" ||
        protocol == "application/pgp-signature")) {

      isgpg = true;
      create_gpg_context ();

    } else {
      log << error << "crypto: unsupported protocol: " << protocol << endl;
      ready = false;
      return;
    }


    ready = true;
  }

  Crypto::~Crypto () {
    log << debug << "crypto: deconstruct." << endl;
  }

  GMimeObject * Crypto::decrypt_and_verify (GMimeObject * part) {
    using std::endl;
    log << debug << "crypto: decrypting and verifiying.." << endl;
    decrypt_tried = true;

    if (!GMIME_IS_MULTIPART_ENCRYPTED (part)) {
      log << error << "crypto: part is not encrypted." << endl;
      return NULL;
    }

    GError *err = NULL;

    GMimeMultipartEncrypted * ep = GMIME_MULTIPART_ENCRYPTED (part);
    GMimeObject * dp = g_mime_multipart_encrypted_decrypt
	(ep, gpgctx, &decrypt_res, &err);

    /* GMimeDecryptResult and GMimeCertificates
     *
     * Only the certificates of the signature are fully populated, the certificates
     * listed in GMimeDecryptResult->recipients are only a list of the alleged
     * key ids of the receivers. These can be spoofed by the sender, or normally set to
     * 0x0 if the receivers should be anonymous (-R for gpg).
     *
     * It is not possible not have a certificate entry for a receiver, so it is always
     * possible to see how many recipients there are for a message.
     *
     * For decrypted messages the certificate only has the key_id field set, the rest
     * of the information has to be fetched manually (note that the key_id is for the
     * subkey that supports encryption - E). When the rest of the information is fetched
     * from an available public key we cannot be sure that this is really the recipient.
     *
     *
     * Encryption and Decryption:
     *
     * The option always_trust on the gpg context must be set if the local key is not
     * trusted (or considered valid). Otherwise it is not used for encryption-target.
     *
     * There is some confusion about the term 'trust' here since it normally means whether
     * you trust the keys signing of other keys, in this case it means 'is the key valid'
     * 'or is the key really the one from the recipient'.
     *
     */

    if (decrypt_res) {
      rlist = g_mime_decrypt_result_get_recipients (decrypt_res);
      slist = g_mime_decrypt_result_get_signatures (decrypt_res);

      for (int i = 0; i < g_mime_certificate_list_length (rlist); i++) {

        GMimeCertificate * ce = g_mime_certificate_list_get_certificate (rlist, i);

        const char * c = NULL;
        ustring fp = (c = g_mime_certificate_get_fingerprint (ce), c ? c : "");
        ustring nm = (c = g_mime_certificate_get_name (ce), c ? c : "");
        ustring em = (c = g_mime_certificate_get_email (ce), c ? c : "");
        ustring key = (c = g_mime_certificate_get_key_id (ce), c ? c : "");

        log << debug << "cr: encrypted for: " << nm << "(" << em << ") [" << fp << "] [" << key << "]" << endl;
      }
    }

    if (dp == NULL) {
      log << error << "crypto: failed to decrypt message: " << err->message << endl;
      decrypted = false;
      decrypt_error = err->message;

    } else {
      log << info << "crypto: successfully decrypted message." << endl;
      decrypted = true;

      verify_tried = (slist != NULL);
      verified = verify_signature_list (slist);
    }


    return dp;
  }

  bool Crypto::verify_signature (GMimeObject * mo) {
    GError * err = NULL;

    verify_tried = true;

    slist = g_mime_multipart_signed_verify (GMIME_MULTIPART_SIGNED(mo), gpgctx, &err);

    verified = verify_signature_list (slist);

    return verified;
  }

  bool Crypto::verify_signature_list (GMimeSignatureList * list) {
    if (list == NULL) return false;

    bool res = g_mime_signature_list_length (list) > 0;

    for (int i = 0; i < g_mime_signature_list_length (list); i++) {
      GMimeSignature * s = g_mime_signature_list_get_signature (list, i);

      res &= g_mime_signature_get_status (s) == GMIME_SIGNATURE_STATUS_GOOD;
    }

    return res;
  }

  bool Crypto::encrypt (GMimeObject * mo, bool sign, ustring userid, InternetAddress * from, ustring to, GMimeMultipartEncrypted ** out, GError ** err)
  {
    return encrypt (mo, sign, userid, from, AddressList (to), out, err);
  }

  bool Crypto::encrypt (GMimeObject * mo, bool sign, ustring userid, InternetAddress * from, AddressList to, GMimeMultipartEncrypted ** out, GError ** err)
  {

    /* build receipients */
    AddressList recp  = to + Address (from);
    recp.remove_duplicates ();

    GPtrArray * recpa = g_ptr_array_sized_new (recp.size ());

    std::vector<ustring> ur;

    for (Address &a : recp.addresses) {
      ur.push_back (a.email ());
    }

    log << debug << "cr: encrypting for: ";
    for (ustring &u : ur) {
      g_ptr_array_add (recpa, (gpointer) u.c_str ());
      log << u << " ";
    }
    log << endl;

    *out = g_mime_multipart_encrypted_new ();

    int r = g_mime_multipart_encrypted_encrypt (
        *out,
        mo,
        gpgctx,
        sign,
        userid.c_str (),
        GMIME_DIGEST_ALGO_DEFAULT,
        recpa,
        err);


    g_ptr_array_free (recpa, false);

    if (r == 0) {
      log << debug << "crypto: successfully encrypted message." << endl;
    } else {
      log << debug << "crypto: failed to encrypt message: " << (*err)->message << endl;
    }

    return (r == 0);
  }

  bool Crypto::sign (GMimeObject * mo, ustring userid, GMimeMultipartSigned ** out, GError ** err) {
    *out = g_mime_multipart_signed_new ();

    int r = g_mime_multipart_signed_sign (
        *out,
        mo,
        gpgctx,
        userid.c_str (),
        GMIME_DIGEST_ALGO_DEFAULT,
        err);

    if (r == 0) {
      log << debug << "crypto: successfully signed message." << endl;
    } else {
      log << debug << "crypto: failed to sign message: " << (*err)->message << endl;
    }

    return (r == 0);
  }

  bool Crypto::create_gpg_context () {
    gpgctx = g_mime_gpg_context_new (NULL, gpgpath.length() ? gpgpath.c_str () : "gpg");
    if (! gpgctx) {
      log << error << "crypto: failed to create gpg context." << std::endl;
      return false;
    }

    g_mime_gpg_context_set_use_agent ((GMimeGpgContext *) gpgctx, TRUE);
    g_mime_gpg_context_set_always_trust ((GMimeGpgContext *) gpgctx, always_trust);
    g_mime_gpg_context_set_auto_key_retrieve ((GMimeGpgContext *) gpgctx, auto_key_retrieve);

    return true;
  }

  GMimeMultipart * Crypto::split_inline_pgp (refptr<Glib::ByteArray> content) {
    /* try to split inline pgp into a multipart with surrounding clear text parts
     * and a GMimeMultiPartEncrypted  or GMimeMultiPartSigned part */

    if (content->size () == 0) return NULL;

    std::string c = reinterpret_cast<char *> (content->get_data ());

    GMimeMultipart * m = g_mime_multipart_new ();

    size_t b = 0;
    size_t last_part = 0;

    while (b = c.find ("BEGIN PGP", b), b != std::string::npos) {

      /* is this an encrypted message or a signed message */
      bool isenc = c.find ("BEGIN PGP MESSAGE", b) == b;
      bool issig = !isenc && c.find ("BEGIN PGP SIGNED MESSAGE", b) == b;

      if (isenc || issig) {
        /* add previous part */
        std::string prt = c.substr (last_part, b - last_part);

        GMimeStream * contentStream = g_mime_stream_mem_new_with_buffer(prt.c_str(), prt.size());
        GMimePart * messagePart = g_mime_part_new_with_type ("text", "plain");

        /* g_mime_object_set_content_type_parameter ((GMimeObject *) messagePart, "charset", astroid->config().get<string>("editor.charset").c_str()); */

        GMimeDataWrapper * contentWrapper = g_mime_data_wrapper_new_with_stream(contentStream, GMIME_CONTENT_ENCODING_DEFAULT);

        /* g_mime_part_set_content_encoding (messagePart, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE); */
        g_mime_part_set_content_object (messagePart, contentWrapper);

        g_mime_multipart_add (m, GMIME_OBJECT (messagePart));

        g_object_unref(messagePart);
        g_object_unref(contentWrapper);
        g_object_unref(contentStream);

        last_part = b;

        /* make new part */
        if (isenc) {
          /* find end */
          size_t e = c.find ("END PGP MESSAGE", b);
          if (e != std::string::npos) {

            std::string prt = "Version: 1.0\n";
            GMimeMultipart * em = g_mime_multipart_new_with_subtype ("encrypted");

            /* add version stuff */
            GMimeStream * contentStream = g_mime_stream_mem_new_with_buffer(prt.c_str(), prt.size());
            GMimePart * messagePart = g_mime_part_new_with_type ("application", "pgp-encrypted");

            /* g_mime_object_set_content_type_parameter ((GMimeObject *) messagePart, "charset", astroid->config().get<string>("editor.charset").c_str()); */

            GMimeDataWrapper * contentWrapper = g_mime_data_wrapper_new_with_stream(contentStream, GMIME_CONTENT_ENCODING_DEFAULT);

            /* g_mime_part_set_content_encoding (messagePart, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE); */
            g_mime_part_set_content_object (messagePart, contentWrapper);

            g_mime_multipart_add (em, GMIME_OBJECT (messagePart));

            g_object_unref(messagePart);
            g_object_unref(contentWrapper);
            g_object_unref(contentStream);

            /* add content */
            prt = c.substr (b, e - b);
            contentStream = g_mime_stream_mem_new_with_buffer(prt.c_str(), prt.size());
            messagePart = g_mime_part_new_with_type ("application", "octet-stream");

            /* g_mime_object_set_content_type_parameter ((GMimeObject *) messagePart, "charset", astroid->config().get<string>("editor.charset").c_str()); */

            contentWrapper = g_mime_data_wrapper_new_with_stream(contentStream, GMIME_CONTENT_ENCODING_DEFAULT);

            /* g_mime_part_set_content_encoding (messagePart, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE); */
            g_mime_part_set_content_object (messagePart, contentWrapper);

            g_mime_multipart_add (em, GMIME_OBJECT (messagePart));

            g_object_unref(messagePart);
            g_object_unref(contentWrapper);
            g_object_unref(contentStream);

            g_mime_multipart_add (m, GMIME_OBJECT(em));

            last_part = e;
            b = e;
          } else {
            /* no end, can't parse the message */

            // TODO: unref..

            return NULL;
          }

        } else if (issig) {

          size_t e = c.find ("END PGP SIGNED MESSAGE", b);
          if (e != std::string::npos) {

          } else {
            // broken message structure
            // TODO: unref..

            return NULL;
          }
        }

      } else {
        /* unknown type, keep going */
        b++;
        continue;
      }
    }


    return m;
  }

  ustring Crypto::get_md5_digest (ustring str) {
    unsigned char * digest = get_md5_digest_char (str);

    std::ostringstream os;
    for (int i = 0; i < 16; i++) {
      os << std::hex << std::setfill('0') << std::setw(2) << ((int)digest[i]);
    }

    delete digest;

    return os.str ();
  }

  unsigned char * Crypto::get_md5_digest_char (ustring str) {
    GMimeStream * mem = g_mime_stream_mem_new ();
    GMimeStream * filter_stream = g_mime_stream_filter_new (mem);

    GMimeFilter * md5f = g_mime_filter_md5_new ();
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), md5f);

    g_mime_stream_write_string (filter_stream, str.c_str ());

    unsigned char *digest = new unsigned char[16];
    g_mime_filter_md5_get_digest (GMIME_FILTER_MD5(md5f), digest);


    g_object_unref (md5f);
    g_object_unref (filter_stream);
    g_object_unref (mem);

    return digest;
  }
}


