<?php
namespace GuzzleHttp\Cookie;

//use Psr\Http\Message\RequestInterface;
//use Psr\Http\Message\ResponseInterface;

/**
 * Stores HTTP cookies.
 *
 * It extracts cookies from HTTP requests, and returns them in HTTP responses.
 * CookieJarInterface instances automatically expire contained cookies when
 * necessary. Subclasses are also responsible for storing and retrieving
 * cookies from a file, database, etc.
 *
 * @link http://docs.python.org/2/library/cookielib.html Inspiration
 */
interface CookieJarInterface extends \Countable, \IteratorAggregate
{
    /**
     * Get Set-Cookie header string suitable for a request to specified scheme, host, path.
     *
     * If no matching cookies are found in the cookie jar, then returns
     * empty string.
     *
     * @param string $scheme  URL scheme
     * @param string $host  URL host
     * @param string $path  URL path
     *
     * @return string a Set-Cookie header string
     */
    public function getCookieHeader(string $scheme, string $host, string $path);

    /**
     * Extract cookie from an HTTP response and store them in the CookieJar.
     *
     * @param string  $setCookie  Set-Cookie value
     * @param string  $host  Host name
     */
    public function extractCookie(string $setCookie, string $host);
    

    /**
     * Sets a cookie in the cookie jar.
     *
     * @param SetCookie $cookie Cookie to set.
     *
     * @return bool Returns true on success or false on failure
     */
    public function setCookie(SetCookie $cookie);

    /**
     * Remove cookies currently held in the cookie jar.
     *
     * Invoking this method without arguments will empty the whole cookie jar.
     * If given a $domain argument only cookies belonging to that domain will
     * be removed. If given a $domain and $path argument, cookies belonging to
     * the specified path within that domain are removed. If given all three
     * arguments, then the cookie with the specified name, path and domain is
     * removed.
     *
     * @param string $domain Clears cookies matching a domain
     * @param string $path   Clears cookies matching a domain and path
     * @param string $name   Clears cookies matching a domain, path, and name
     *
     * @return CookieJarInterface
     */
    public function clear($domain = null, $path = null, $name = null);

    /**
     * Discard all sessions cookies.
     *
     * Removes cookies that don't have an expire field or a have a discard
     * field set to true. To be called when the user agent shuts down according
     * to RFC 2965.
     */
    public function clearSessionCookies();

    /**
     * Converts the cookie jar to an array.
     *
     * @return array
     */
    public function toArray();
}
