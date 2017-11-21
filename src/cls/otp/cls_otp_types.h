#ifndef CEPH_CLS_OTP_TYPES_H
#define CEPH_CLS_OTP_TYPES_H

#include "include/encoding.h"
#include "include/types.h"


#define CLS_OTP_MAX_REPO_SIZE 100


namespace rados {
  namespace cls {
    namespace otp {

      enum OTPType {
        OTP_UNKNOWN = 0,
        OTP_HOTP = 1,  /* unsupported */
        OTP_TOTP = 2,
      };

      struct otp_info_t {
        OTPType type{OTP_TOTP};
        string id;
        string seed;
        ceph::real_time time_ofs;
        uint32_t step_size{30}; /* num of seconds foreach otp to test */
        uint32_t window{2}; /* num of otp after/before start otp to test */

        otp_info_t() {}

        void encode(bufferlist &bl) const {
          ENCODE_START(1, 1, bl);
          ::encode((uint8_t)type, bl);
          /* if we ever implement anything other than TOTP
           * then we'll need to branch here */
          ::encode(id, bl);
          ::encode(seed, bl);
          ::encode(time_ofs, bl);
          ::encode(step_size, bl);
          ::encode(window, bl);
          ENCODE_FINISH(bl);
        }
        void decode(bufferlist::iterator &bl) {
          DECODE_START(1, bl);
          uint8_t t;
          ::decode(t, bl);
          type = (OTPType)t;
          ::decode(id, bl);
          ::decode(seed, bl);
          ::decode(time_ofs, bl);
          ::decode(step_size, bl);
          ::decode(window, bl);
          DECODE_FINISH(bl);
        }
        void dump(Formatter *f) const;
      };
      WRITE_CLASS_ENCODER(rados::cls::otp::otp_info_t)

      enum OTPCheckResult {
        OTP_CHECK_UNKNOWN = 0,
        OTP_CHECK_SUCCESS = 1,
        OTP_CHECK_FAIL = 2,
      };

      struct otp_check_t {
        string token;
        ceph::real_time timestamp;
        OTPCheckResult result{OTP_CHECK_UNKNOWN};

        void encode(bufferlist &bl) const {
          ENCODE_START(1, 1, bl);
          ::encode(token, bl);
          ::encode(timestamp, bl);
          ::encode((char)result, bl);
          ENCODE_FINISH(bl);
        }
        void decode(bufferlist::iterator &bl) {
          DECODE_START(1, bl);
          ::decode(token, bl);
          ::decode(timestamp, bl);
          uint8_t t;
          ::decode(t, bl);
          result = (OTPCheckResult)t;
          DECODE_FINISH(bl);
        }
      };
      WRITE_CLASS_ENCODER(rados::cls::otp::otp_check_t)

      struct otp_repo_t {
        map<string, otp_info_t> entries;

        otp_repo_t() {}

        void encode(bufferlist &bl) const {
          ENCODE_START(1, 1, bl);
          ::encode(entries, bl);
          ENCODE_FINISH(bl);
        }
        void decode(bufferlist::iterator &bl) {
          DECODE_START(1, bl);
          ::decode(entries, bl);
          DECODE_FINISH(bl);
        }
      };
      WRITE_CLASS_ENCODER(rados::cls::otp::otp_repo_t)
    }
  }
}

WRITE_CLASS_ENCODER(rados::cls::otp::otp_info_t)
WRITE_CLASS_ENCODER(rados::cls::otp::otp_check_t)
WRITE_CLASS_ENCODER(rados::cls::otp::otp_repo_t)

#endif