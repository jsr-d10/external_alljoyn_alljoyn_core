/*
 * Copyright 2010 - 2011, Qualcomm Innovation Center, Inc.
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
 *
 */
package org.alljoyn.bus.samples.simpleclient;

import android.content.Context;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.TextView;
import android.widget.LinearLayout;

public class BusNameItemAdapter extends ArrayAdapter<BusNameItem> {

    private Client client;
    private Context context;

    public BusNameItemAdapter(Context context, Client client) {
        super(context, R.id.BusName);
        this.context = context;
        this.client = client;
    }

    public View getView(int position, View convertView, ViewGroup parent) {
        LinearLayout itemLayout;
        itemLayout = (LinearLayout) LayoutInflater.from(context).inflate(R.layout.service, parent, false);

        final BusNameItem n = getItem(position);
        final TextView busNameView = (TextView) itemLayout.findViewById(R.id.BusName);
        final Client c = client;

        final Button connectButton = (Button) itemLayout.findViewById(R.id.Connect);
        final Button pingButton = (Button) itemLayout.findViewById(R.id.Ping);
        final BusNameItemAdapter adapter = this;

        connectButton.setText(n.isConnected() ? "Disconnect" : "Connect");
        connectButton.setOnClickListener(new Button.OnClickListener() {
                public void onClick(View v) {
                    int id = n.getSessionId();
                    if (id != 0) {
                        c.leaveSession(id);
                        n.setSessionId(0);
                        connectButton.setText("Connect");
                        pingButton.setEnabled(false);
                    } else {
                        int sessionId = c.joinSession(n.getBusName());
                        if (sessionId != 0) {
                            n.setSessionId(sessionId);
                            connectButton.setText("Disconnect");
                            pingButton.setEnabled(true);
                        } else {
                            adapter.remove(n);
                        }
                    }
                    c.hideInputMethod();
                    busNameView.invalidate();
                }
            });
        
        pingButton.setEnabled(n.isConnected());
        pingButton.setTag(R.id.client, client);
        pingButton.setTag(R.id.foundName, n);
        pingButton.setOnClickListener(new Button.OnClickListener() {
                public void onClick(View v) {
                    c.ping(n.getSessionId(), n.getBusName());
                    c.hideInputMethod();
                }
            });

        busNameView.setText(n.getBusName());
        return itemLayout;
    }
}
