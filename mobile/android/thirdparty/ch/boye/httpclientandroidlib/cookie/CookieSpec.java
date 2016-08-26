/*
 * ====================================================================
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 */

package ch.boye.httpclientandroidlib.cookie;

import java.util.List;

import ch.boye.httpclientandroidlib.Header;

/**
 * Defines the cookie management specification.
 * <p>Cookie management specification must define
 * <ul>
 *   <li> rules of parsing "Set-Cookie" header
 *   <li> rules of validation of parsed cookies
 *   <li>  formatting of "Cookie" header
 * </ul>
 * for a given host, port and path of origin
 *
 *
 * @since 4.0
 */
public interface CookieSpec {

    /**
     * Returns version of the state management this cookie specification
     * conforms to.
     *
     * @return version of the state management specification
     */
    int getVersion();

    /**
      * Parse the <tt>"Set-Cookie"</tt> Header into an array of Cookies.
      *
      * <p>This method will not perform the validation of the resultant
      * {@link Cookie}s</p>
      *
      * @see #validate
      *
      * @param header the <tt>Set-Cookie</tt> received from the server
      * @param origin details of the cookie origin
      * @return an array of <tt>Cookie</tt>s parsed from the header
      * @throws MalformedCookieException if an exception occurs during parsing
      */
    List<Cookie> parse(Header header, CookieOrigin origin) throws MalformedCookieException;

    /**
      * Validate the cookie according to validation rules defined by the
      *  cookie specification.
      *
      * @param cookie the Cookie to validate
      * @param origin details of the cookie origin
      * @throws MalformedCookieException if the cookie is invalid
      */
    void validate(Cookie cookie, CookieOrigin origin) throws MalformedCookieException;

    /**
     * Determines if a Cookie matches the target location.
     *
     * @param cookie the Cookie to be matched
     * @param origin the target to test against
     *
     * @return <tt>true</tt> if the cookie should be submitted with a request
     *  with given attributes, <tt>false</tt> otherwise.
     */
    boolean match(Cookie cookie, CookieOrigin origin);

    /**
     * Create <tt>"Cookie"</tt> headers for an array of Cookies.
     *
     * @param cookies the Cookies format into a Cookie header
     * @return a Header for the given Cookies.
     * @throws IllegalArgumentException if an input parameter is illegal
     */
    List<Header> formatCookies(List<Cookie> cookies);

    /**
     * Returns a request header identifying what version of the state management
     * specification is understood. May be <code>null</code> if the cookie
     * specification does not support <tt>Cookie2</tt> header.
     */
    Header getVersionHeader();

}
