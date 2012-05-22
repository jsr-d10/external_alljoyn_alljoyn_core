/*
 * Copyright 2012, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

package org.alljoyn.proximity;

import org.alljoyn.bus.annotation.Position;
import org.alljoyn.bus.annotation.Signature;

public class ScanResultMessage {

	@Position(0)
	@Signature("s")
	public String bssid;

	@Position(1)
	@Signature("s")
	public String ssid;
	
	@Position(2)
	@Signature("b")
	public boolean attached;
	
}
