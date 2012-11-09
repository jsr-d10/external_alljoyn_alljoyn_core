/*
 * Copyright 2010-2011, Qualcomm Innovation Center, Inc.
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

package org.alljoyn.bus.alljoyn;

import android.app.Service;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.IBinder;
import android.util.Log;
import java.util.ArrayList;

public class BundleDaemonService extends Service {
    private static final String TAG = "BundleDaemonService";
    static {
        System.loadLibrary("daemon-jni");
    }

    public static native int runDaemon(Object[] argv, String config);

    private BundleDaemonThread thread;

    class BundleDaemonThread extends Thread {
        public void run()
        {
            WifiManager wifi = (android.net.wifi.WifiManager)getSystemService(android.content.Context.WIFI_SERVICE);
            WifiManager.MulticastLock lock = wifi.createMulticastLock("BundleDaemonService");
            lock.setReferenceCounted(false);
            lock.acquire();

        	AllJoynDaemon.runDaemon(mArgv.toArray(), mConfig);

            lock.release();
        }
    }

    private ArrayList<String> mArgv = new ArrayList<String>();
    private String mConfig;
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        String connectSpec = intent.getStringExtra("ConnectSpec");
        Log.i(TAG, "mThread.run() " + connectSpec);
        mArgv.clear();
        mArgv.add("BundleDaemon");
        mArgv.add("--config-service");
        mArgv.add("--nofork");
        mArgv.add("--no-bt");              

        mConfig = 
                "<busconfig>" + 
                "  <type>alljoyn</type>" + 
                "  <listen> "+
                connectSpec +
                "</listen>" + 
                "  <listen>tcp:addr=0.0.0.0,port=0</listen>" +
                "  <policy context=\"default\">" +
                "    <allow send_interface=\"*\"/>" +
                "    <allow receive_interface=\"*\"/>" +
                "    <allow own=\"*\"/>" +
                "    <allow user=\"*\"/>" +
                "    <allow send_requested_reply=\"true\"/>" +
                "    <allow receive_requested_reply=\"true\"/>" +
                "  </policy>" +
                "  <limit auth_timeout=\"32768\"/>" +
                "  <limit max_incomplete_connections=\"16\"/>" +
                "  <limit max_completed_connections=\"64\"/>" +
                "  <alljoyn module=\"ipns\">" +
                "    <property interfaces=\"*\"/>" +
                "  </alljoyn>" +
                "</busconfig>";
            Log.i(TAG, "mThread.run(): returned from runDaemon().  Self-immolating.");        	    	
            thread = new BundleDaemonThread();
            thread.start();
    	
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent)
    {
        return null;
    }
}
