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
package org.alljoyn.bus.samples.simpleservice;


import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Toast;

public class Service extends Activity {

    private ArrayAdapter<String> listViewArrayAdapter;
    private ListView listView;
    private Menu menu;

    /** Called when activity's onCreate is called */
    private native int simpleOnCreate();

    /** Called when activity's onDestroy is called */
    private native void simpleOnDestroy();

    /** Called to start service with a given well-known name */
    private native boolean startService(String name, String packageName);

    /** Called to stop service */
    private native void stopService(String name);

    HandlerThread bus_thread = new HandlerThread("BusHandler");
    { bus_thread.start(); }
    Handler bus_handler = new Handler(bus_thread.getLooper());

    /** Handler used to post messages from C++ into UI thread */
    private Handler handler = new Handler();

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {

        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        listViewArrayAdapter = new ArrayAdapter<String>(this, R.layout.message);
        listView = (ListView) findViewById(R.id.ListView);
        listView.setAdapter(listViewArrayAdapter);

        Button startButton = (Button) findViewById(R.id.StartButton);
        startButton.setOnClickListener(new Button.OnClickListener() {
            public void onClick(final View v) {
                final EditText edit = (EditText) findViewById(R.id.AdvertisedName);

                bus_handler.post(new Runnable() {
                    // called from BusHandler thread!
                    public void run() {
                        if (startService(edit.getText().toString(), getPackageName())) {
                            handler.post(new Runnable() {
                                // post Runnable object back to main thread
                                public void run() {
                                    Button startButton = (Button) v;
                                    startButton.setEnabled(false);
                                    Button stopButton = (Button) findViewById(R.id.StopButton);
                                    stopButton.setEnabled(true);
                                    hideInputMethod(edit);
                                    edit.setEnabled(false);
                                }
                            });
                        }
                    }
                });
            }
        });

        Button stopButton = (Button) findViewById(R.id.StopButton);
        stopButton.setEnabled(false);

        stopButton.setOnClickListener(new Button.OnClickListener() {
            public void onClick(final View v) {
                final EditText edit = (EditText) findViewById(R.id.AdvertisedName);

                bus_handler.post(new Runnable() {
                    // called from BusHandler thread!
                    public void run() {
                        stopService(edit.getText().toString());

                        handler.post(new Runnable() {
                            public void run() {
                                Button stopButton = (Button) v;
                                stopButton.setEnabled(false);
                                Button startButton = (Button) findViewById(R.id.StartButton);
                                startButton.setEnabled(true);
                                edit.setEnabled(true);
                            }
                        });
                    }
                });
            }
        });

        // Initialize the native part of the sample and connect to remote alljoyn instance
        bus_handler.post(new Runnable() {
            public void run() {
                final int ret = simpleOnCreate();
                if (0 != ret) {
                    handler.post(new Runnable() {
                        public void run() {
                            Toast.makeText(Service.this, "simpleOnCreate failed with " + ret, Toast.LENGTH_LONG).show();
                            finish();
                        }
                    });
                }
            }
        });
    }

    @Override
    protected void onStop() {
        super.onStop();
        // Don't let this app run once it is off screen (it is confusing for a demo)
        this.finish();
    }

    @Override
    protected void onDestroy() {
        simpleOnDestroy();
        bus_handler.getLooper().quit();
        super.onDestroy();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        MenuInflater inflater = getMenuInflater();
        inflater.inflate(R.menu.mainmenu, menu);
        this.menu = menu;
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle item selection
        switch (item.getItemId()) {
        case R.id.quit:
            finish();
            return true;

        default:
            return super.onOptionsItemSelected(item);
        }
    }

    public void hideInputMethod(View focusView)
    {
        InputMethodManager inputManager = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        inputManager.hideSoftInputFromWindow(focusView.getWindowToken(), 0);
    }

    public void PingCallback(final String sender, final String pingStr) {
        handler.post(new Runnable() {
            public void run() {
                String s = String.format("Message from %s: \"%s\"", sender.substring(sender.length() - 10), pingStr);
                listViewArrayAdapter.add(s);
                listView.setSelection(listView.getBottom());
            }
        });
    }


    static {
        System.loadLibrary("SimpleService");
    }
}