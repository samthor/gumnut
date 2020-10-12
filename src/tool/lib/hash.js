
import crypto from 'crypto';

/**
 * Hashes the passed buffer.
 *
 * @param {!Buffer}
 */
export function hashBuffer(buffer) {
  const hash = crypto.createHash('sha256');
  hash.update(buffer);
  return hash.digest('hex');
}
